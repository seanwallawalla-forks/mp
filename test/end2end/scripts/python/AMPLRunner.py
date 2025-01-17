from threading import Lock
import math
from pathlib import Path
from shutil import which

from Solver import Solver
from amplpy import AMPL, Kind, OutputHandler, ErrorHandler, Runnable
from Model import Model


class InnerOutputHandler(OutputHandler):
    def __init__(self, storeOutput = False):
      self._storeOutput = storeOutput
      if storeOutput:
        self._msgs = list()
        self._kinds = list()
    def output(self, kind, msg):
      if self._storeOutput:
        self._kinds.append(kind)
        self._msgs.append(msg)
      pass
    def getMessages(self):
      return self._msgs
    def getKinds(self):
      return self._kinds

class InnerErrorHandler(ErrorHandler):
  def __init__(self, appendError):
    self._appendError=appendError
  def error(self, exc):
    self._appendError(exc)
    pass
  def warning(self, exc): 
    pass

class AMPLRunner(object):

    def __init__(self, solver=None, writeSolverName = False,
                 keepAMPLOutput = False):
        if solver:
            self.setSolver(solver)
        else:
            self._solver = None
        self._writeSolverName = writeSolverName
        self._amplInitialized = False
        self._keepAMPLOutput = keepAMPLOutput

    def _initAMPL(self):
        if self._amplInitialized:
          self._ampl.reset()
          return
        self._ampl = AMPL()
        self._outputHandler = InnerOutputHandler(storeOutput = self._keepAMPLOutput)
        self._ampl.setOutputHandler(self._outputHandler)
        self._ampl.setErrorHandler(InnerErrorHandler(self.appendError))
        self._ampl.setOption("solver_msg", 0)
        if self._solver:
          self._setSolverInAMPL()
        self._amplInitialized = True
   
    def _terminateAMPL(self):
      self._ampl.close()
      self._amplInitialized = False

    def appendError(self, exception):
      self._lastError = exception

    def readModel(self, model: Model):
        mp = Path(model.getFilePath())
        if not mp.exists():
            raise Exception("Model {} not found".format(model.getFilePath()))
        if model.isScript():
          class MyInterpretIsOver(Runnable):
            executed = False
            def run(self):
              self.executed = True
              mutex.release()
          callback = MyInterpretIsOver()
          mutex = Lock()
          mutex.acquire()
          timeOut = self._solver.getTimeout()
          self._ampl.evalAsync("include '{}';".format(str(mp.absolute().resolve())), callback)
          mutex.acquire(timeout=timeOut)
          if not callback.executed:
            self._ampl.interrupt()
            self._ampl.interrupt()
            return None
        else:
          self._ampl.read(str(mp.absolute().resolve()))
          files = model.getAdditionalFiles()
          if files:
              for f in files:
                  fp = Path(f)
                  if fp.suffix == ".dat":
                      self._ampl.readData(str(f.absolute().resolve()))
                  else:
                      self._ampl.read(str(f.absolute().resolve()))
        return mp

    def writeNL(self, model, outdir=None):
        """ Write an NL file corresponding to the specified model. 
            By default it writes in the model directory, unless outdir is specified"""
        self._initAMPL()
        mp = self.readModel(model)
        if outdir:
            dir = str(Path(outdir).absolute().resolve())
        else:
            dir = str(mp.parent.resolve())

        self._ampl.cd(dir)
        nlname = "g{}".format(mp.stem)
        self._ampl.eval("write '{}';".format(nlname))
        self._terminateAMPL()

    def setSolver(self, solver: Solver):
        self._solver = solver
        sp = which(solver.getExecutable())
        if sp is None:
            raise Exception("Solver '{}' not found.".
                format(solver.getExecutable()))

    def _setSolverInAMPL(self):
        sp = self._solver.getExecutable()
        self._ampl.setOption("solver", sp)
        (name, value) = self._solver.getAMPLOptions()
        self._ampl.setOption(name, value)

    def tryGetObjective(self):
      try: 
          return self._ampl.getCurrentObjective().value()
      except:
          try:
             # Get the first objective
             objs = self._ampl.getObjectives()
             obj = next(objs)[1]
             try:
               # Try as scalar objective
               return obj.value()
             except Exception as e: 
               pass
             for index, instance in obj:
                 # Return the first instance if indexed
                 return instance.value()
          except Exception as e:
            pass
      return None

    def doInit(self, model: Model):
        self.stats = { "solver": self._solver.getName() }
        self._initAMPL()
        self._lastError = None

    def printInitMessage(self, model: Model):
      msg = "Generating and solving {}".format(model.getName())
      if self._solver:
        if  self._writeSolverName:
            msg += " with {}".format(self._solver.getName())
        if self._solver.getNThreads():
            msg += " using {} threads".format(
                self._solver.getNThreads())
      print( "{0: <80}".format(msg), end="", flush=True)

    def doReadModel(self, model:Model):
        mp = str(Path(model.getFilePath()).parent.absolute().resolve())
        if model.isScript():
            self._ampl.cd(mp)
        return self.readModel(model)

    def runAndEvaluate(self, model: Model):
        self._run(model)
        self._evaluateRun(model)
        # Todo: check bug in the API (or in amplpy) by which old objectives are 
        # reported after reset. Terminating AMPL each time takes care of the problem
        # but it's hardly efficient
        self._terminateAMPL()

    def _run(self, model: Model):
      self.doInit(model)
      self.setupOptions(model)
      mp = self.doReadModel(model)

      if model.isScript() and mp == None: # if a script ran out of time (had to kill AMPL)
         self._terminateAMPL()
         self.stats["solutionTime"] = self._solver.getTimeout()
         self.stats["objective"] = None
         if self._lastError:
           self.stats["outmsg"] = self._lastError
         else:
           self.stats["outmsg"] = "Script ran out of time"
         self.stats["timelimit"] = True
         return

      if not model.isScript():
          self._ampl.solve()
      interval = self._ampl.getValue("_solve_elapsed_time")
      self.stats["solutionTime"] = interval
      v = self.tryGetObjective()
      self.stats["objective"] = v
      if self._lastError:
        self.stats["outmsg"] = str(self._lastError)
        self.stats["timelimit"] = self._ampl.getValue("solve_result")
      else:
        self.stats["outmsg"] = self._ampl.getValue("solve_message")
        self.stats["timelimit"] = self._ampl.getValue("solve_result")
      return

    def setupOptions(self, model: Model):
        if model.hasOptions():
            optmap = model.getOptions()
            for name, val in optmap.items():
                (slvname, slvval) = self._solver.getAMPLOptions()
                if name.endswith("SOLVER_options"):               # Any-solver option
                    if not slvname in optmap:
                        name = slvname
                    else:
                        continue                                  # Skip as solver-specific given
                if slvname==name:
                    val = slvval + ' ' + val                      # Prepend 'desired' options like nthreads
                self._ampl.setOption(name, val)

    def _evaluateRun(self, model: Model):
        expsol = model.getExpectedObjective()
        if expsol is not None:
            self.stats["eval_done"] = True
            self._assertAndRecord(expsol, self.stats["objective"],
                                  "objective")
        if model.hasExpectedValues():
            for name, ev in model.getExpectedValues().items():
                self.stats["eval_done"] = True
                try:
                    val = self._ampl.getValue(name)
                    self._assertAndRecord(ev, val,
                        "value of entity '{}'".format(name))
                except:
                    self.stats["eval_fail_msg"] = "error retrieving '{}'".format(name)

    def _assertAndRecord(self, expval, val, msg):
        b1 = isinstance(expval, (int, float))
        b2 = isinstance(val, (int, float))
        uneq = not math.isclose(expval, val, rel_tol=1e-6) if \
            b1 and b2 else expval != val
        if uneq:
            self.stats["eval_fail_msg"] = msg + \
                ": value " + str(val) + \
                ", expected " + str(expval)

    def getName(self):
        return "ampl-" + self._solver.getName()

    def getExecutable(self):
        return self._solver._exePath

    def getSolver(self):
        return self._solver

    def getSolutionStats(self):
        return self.stats
