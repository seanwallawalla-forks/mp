/*
 Abstract solver backend wrapper.

 Copyright (C) 2021 AMPL Optimization Inc

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that the copyright notice and this permission notice and warranty
 disclaimer appear in supporting documentation.

 The author and AMPL Optimization Inc disclaim all warranties with
 regard to this software, including all implied warranties of
 merchantability and fitness.  In no event shall the author be liable
 for any special, indirect or consequential damages or any damages
 whatsoever resulting from loss of use, data or profits, whether in an
 action of contract, negligence or other tortious action, arising out
 of or in connection with the use or performance of this software.
*/

#ifndef BACKEND_H_
#define BACKEND_H_

#include <cmath>
#include <functional>
#include <stdexcept>

#include "mp/clock.h"
#include "mp/convert/converter_query.h"
#include "mp/convert/constraint_keeper.h"
#include "mp/convert/std_constr.h"
#include "mp/convert/std_obj.h"
#include "mp/convert/model.h"
#include "mp/convert/model_adapter.h"

/// Issue this if you redefine std feature switches
#define USING_STD_FEATURES using BaseBackend::STD_FEATURE_QUERY_FN
/// Default switch for unmentioned std features,
/// can be used to override base class' settings
#define DEFAULT_STD_FEATURES_TO( val ) \
  template <class FeatureStructName> \
  static constexpr bool STD_FEATURE_QUERY_FN( \
    const FeatureStructName& ) { return val; }
/// And default them
DEFAULT_STD_FEATURES_TO( false )
#define DEFINE_STD_FEATURE( name ) \
  struct STD_FEATURE_STRUCT_NM( name ) { };
#define ALLOW_STD_FEATURE( name, val ) \
  static constexpr bool STD_FEATURE_QUERY_FN( \
    const STD_FEATURE_STRUCT_NM( name )& ) { return val; }
#define IMPL_HAS_STD_FEATURE( name ) MP_DISPATCH_STATIC( \
  STD_FEATURE_QUERY_FN( \
    STD_FEATURE_STRUCT_NM( name )() ) )
#define STD_FEATURE_QUERY_FN AllowStdFeature__func
#define STD_FEATURE_STRUCT_NM( name ) StdFeatureDesc__ ## name

#define UNSUPPORTED(name) \
  throw MakeUnsupportedError( name )


namespace mp {

/// Basic backend wrapper.
/// The basic wrapper provides common functionality: option handling
/// and placeholders for solver API
template <class Impl>
class BasicBackend :
    public BasicConstraintAdder,
    private SolverImpl< ModelAdapter< BasicModel<> > >   // mp::Solver stuff, hidden
{
  ////////////////////////////////////////////////////////////////////////////
  ///////////////////// TO IMPLEMENT IN THE FINAL CLASS //////////////////////
  ////////////////////////////////////////////////////////////////////////////
public:
  static const char* GetSolverName() { return "SomeSolver"; }
  static std::string GetSolverVersion() { return "1.0.0"; }
  /// Whatever the binary is called
  static const char* GetSolverInvocationName() { return "solverdirect"; }
  static const char* GetAMPLSolverLongName() { return nullptr; }
  static const char* GetBackendName()    { return "BasicBackend"; }
  static const char* GetBackendLongName() { return nullptr; }
  static long Date() { return MP_DATE; }

  ArrayRef<double> PrimalSolution()
  { UNSUPPORTED("PrimalSolution()"); return {}; }
  double ObjectiveValue() const
  { UNSUPPORTED("ObjectiveValue()"); return 0.0; }

  ////////////////////////////////////////////////////////////
  /////////////// OPTIONAL STANDARD FEATURES /////////////////
  /////////////// DISALLOW BY DEFAULT /////////////////
  /////////////// PLACEHOLDERS FOR CORR. API /////////////////
  ////////////////////////////////////////////////////////////
protected:
  /// Dual solution. Returns empty if not available
  ArrayRef<double> DualSolution() { return {}; }
  /**
   * MULTIOBJ support
   */
  DEFINE_STD_FEATURE( MULTIOBJ )
  ALLOW_STD_FEATURE( MULTIOBJ, false )
  ArrayRef<double> ObjectiveValues() const
  { UNSUPPORTED("ObjectiveValues()"); return {}; }
  void ObjPriorities(ArrayRef<int>)
  { UNSUPPORTED("BasicBackend::ObjPriorities"); }
  void ObjWeights(ArrayRef<double>)
  { UNSUPPORTED("BasicBackend::ObjWeights"); }
  void ObjAbsTol(ArrayRef<double>)
  { UNSUPPORTED("BasicBackend::ObjAbsTol"); }
  void ObjRelTol(ArrayRef<double>)
  { UNSUPPORTED("BasicBackend::ObjRelTol"); }
  /**
   * MULTISOL support
   * No API to overload,
   *  Impl should check need_multiple_solutions()
   *  and call ReportIntermediateSolution(obj, x, pi) for each
   **/
  DEFINE_STD_FEATURE( MULTISOL )
  ALLOW_STD_FEATURE( MULTISOL, false )
  /**
  * Kappa estimate
  **/
  DEFINE_STD_FEATURE( KAPPA )
  ALLOW_STD_FEATURE( KAPPA, false )
  double Kappa()
  { UNSUPPORTED("BasicBackend::Kappa"); }
  /**
  * FeasRelax
  * No API to overload,
  * Impl should check feasrelax_IOdata()
  **/
  DEFINE_STD_FEATURE( FEAS_RELAX )
  ALLOW_STD_FEATURE( FEAS_RELAX, false )
      /**
      * MIP solution rounding
      * Nothing to do for the Impl, enabled by default
      **/
      DEFINE_STD_FEATURE( WANT_ROUNDING )
      ALLOW_STD_FEATURE( WANT_ROUNDING, true )


  ////////////////////////////////////////////////////////////////////////
  ///////////////////////////// MODEL MANIP //////////////////////////////
  ////////////////////////////////////////////////////////////////////////
public:
  using Model = BasicModel<>;
  using Variable = typename Model::Variable;

  /// Chance for the Backend to init solver environment, etc
  void InitOptionParsing() { }
  /// Chance to consider options immediately (open cloud, etc)
  void FinishOptionParsing() { }

  void InitProblemModificationPhase() { }
  void FinishProblemModificationPhase() { }
  void AddVariable(Variable var) {
    throw MakeUnsupportedError("BasicBackend::AddVariable");
  }
  void AddCommonExpression(Problem::CommonExpr cexpr) {
    throw MakeUnsupportedError("BasicBackend::AddCommonExpressions");
  }
  void AddLogicalConstraint(Problem::LogicalCon lcon) {
    throw MakeUnsupportedError("BasicBackend::AddLogicalConstraints");
  }

  void AddObjective(typename Model::Objective obj) {
    if (obj.nonlinear_expr()) {
      MP_DISPATCH( AddGeneralObjective( obj ) );
    } else {
      LinearExprUnzipper leu(obj.linear_expr());
      LinearObjective lo { obj.type(),
            std::move(leu.c_), std::move(leu.v_) };
      if (nullptr==obj.p_extra_info()) {
        MP_DISPATCH( SetLinearObjective( obj.index(), lo ) );
      } else {
        auto qt = obj.p_extra_info()->qt_;
        assert(!qt.empty());
        MP_DISPATCH( SetQuadraticObjective( obj.index(),
                       QuadraticObjective{std::move(lo), std::move(qt)} ) );
      }
    }
  }
  void AddGeneralObjective(typename Model::Objective ) {
    throw MakeUnsupportedError("BasicBackend::AddGeneralObjective");
  }
  void SetLinearObjective( int, const LinearObjective& ) {
    throw MakeUnsupportedError("BasicBackend::AddLinearObjective");
  }
  void SetQuadraticObjective( int, const QuadraticObjective& ) {
    throw MakeUnsupportedError("BasicBackend::AddQuadraticObjective");
  }

  void AddAlgebraicConstraint(typename Model::AlgebraicCon con) {
    if (con.nonlinear_expr()) {
      MP_DISPATCH( AddGeneralConstraint( con ) );
    } else {
      LinearExprUnzipper leu(con.linear_expr());
      auto lc = LinearConstraint{
          std::move(leu.c_), std::move(leu.v_),
          con.lb(), con.ub() };
      if (nullptr==con.p_extra_info()) {
        MP_DISPATCH( AddConstraint( lc ) );
        orig_lin_constr_.push_back(n_alg_constr_);
      } else {
        auto qt = con.p_extra_info()->qt_;
        assert(!qt.empty());
        MP_DISPATCH( AddConstraint( QuadraticConstraint{std::move(lc), std::move(qt)} ) );
      }
      ++n_alg_constr_;
    }
  }

  void AddGeneralConstraint(typename Model::AlgebraicCon ) {
    throw MakeUnsupportedError("BasicBackend::AddGeneralConstraint");
  }

  ////////////////// Some basic custom constraints /////////////////
  USE_BASE_CONSTRAINT_HANDLERS(BasicConstraintAdder)

  /// Optionally exclude LDCs from being posted,
  /// then all those are converted to LinearConstraint's first
  ACCEPT_CONSTRAINT(LinearDefiningConstraint, NotAccepted)
  void AddConstraint(const LinearDefiningConstraint& ldc) {
    MP_DISPATCH( AddConstraint(ldc.to_linear_constraint()) );
  }

  ACCEPT_CONSTRAINT(LinearConstraint, Recommended)
  /// TODO Attributes (lazy/user cut, etc)
  void AddConstraint(const LinearConstraint& ) {
    throw MakeUnsupportedError("BasicBackend::AddLinearConstraint");
  }


  ////////////////////////////////////////////////////////////////////////////
  /////////////////////////// BASIC PROCESS LOGIC ////////////////////////////
  ////////////////////////////////////////////////////////////////////////////
  void SolveAndReport() {
    MP_DISPATCH( InputExtras() );

    MP_DISPATCH( SetupTimerAndInterrupter() );
    MP_DISPATCH( SolveAndReportIntermediateResults() );
    MP_DISPATCH( RecordSolveTime() );

    MP_DISPATCH( ObtainSolutionStatus() );
    MP_DISPATCH( ReportResults() );
    if (MP_DISPATCH( timing() ))
      MP_DISPATCH( PrintTimingInfo() );
  }

  void InputExtras() {
    MP_DISPATCH( InputStdExtras() );
    MP_DISPATCH( InputCustomExtras() );
  }

  void InputStdExtras() {
    if (multiobj()) {
      MP_DISPATCH( ObjPriorities( ReadSuffix(suf_objpriority) ) );
      MP_DISPATCH( ObjWeights( ReadSuffix(suf_objweight) ) );
      MP_DISPATCH( ObjAbsTol( ReadSuffix(suf_objabstol) ) );
      MP_DISPATCH( ObjRelTol( ReadSuffix(suf_objreltol) ) );
    }
    if (feasrelaxMode())
      MP_DISPATCH( InputFeasRelaxData() );
  }
  void InputCustomExtras() { }

  void SetupTimerAndInterrupter() {
    MP_DISPATCH( SetupInterrupter() );
    MP_DISPATCH( SetupTimer() );
  }

  void SetupInterrupter() {
    MP_DISPATCH( SetInterrupter(MP_DISPATCH( interrupter() )) );
  }

  void SetupTimer() {
    stats.setup_time = GetTimeAndReset(stats.time);
  }

  void RecordSolveTime() {
    stats.solution_time = GetTimeAndReset(stats.time);
  }

  void ObtainSolutionStatus() {
    solve_status_ = MP_DISPATCH(
          ConvertSolutionStatus(*MP_DISPATCH( interrupter() ), solve_code_) );
  }

  void InputFeasRelaxData() {
    auto suf_lbpen = ReadDblSuffix( {"lbpen", suf::VAR} );
    auto suf_ubpen = ReadDblSuffix( {"ubpen", suf::VAR} );
    auto suf_rhspen = ReadDblSuffix( {"lbpen", suf::CON} );
    if (suf_lbpen.empty() && suf_ubpen.empty() && suf_rhspen.empty() &&
        0.0>lbpen() && 0.0>ubpen() && 0.0>rhspen())
      return;
    feasrelax_IOdata().mode_ = feasrelaxMode();
    feasrelax_IOdata().lbpen = FillFeasRelaxPenalty(suf_lbpen, lbpen(),
                MP_DISPATCH( NumberOfVariables() ));
    feasrelax_IOdata().ubpen = FillFeasRelaxPenalty(suf_ubpen, ubpen(),
                MP_DISPATCH( NumberOfVariables() ));
    feasrelax_IOdata().rhspen = FillFeasRelaxPenalty(suf_rhspen, rhspen(),
                MP_DISPATCH( NumberOfConstraints() ));
  }

  using Solver::need_multiple_solutions;

  void ReportResults() {
    MP_DISPATCH( ReportSuffixes() );
    MP_DISPATCH( ReportSolution() );
  }

  void ReportSuffixes() {
    MP_DISPATCH( ReportStandardSuffixes() );
    MP_DISPATCH( ReportCustomSuffixes() );
  }

  void ReportStandardSuffixes() {
    if (MP_DISPATCH(IsProblemSolved()) &&
      exportKappa()) {
      MP_DISPATCH(ReportKappa());
    }
  }
  void ReportKappa() {
    if (exportKappa() && 2)
    {
      double value = MP_DISPATCH(Kappa());
      ReportSingleSuffix(suf_objkappa, value);
      ReportSingleSuffix(suf_probkappa, value);
    }
  }

  void ReportCustomSuffixes() { }

  /// Callback
  void ReportIntermediateSolution(
        double obj_value,
        ArrayRef<double> solution, ArrayRef<double> dual_solution) {
    fmt::MemoryWriter writer;
    writer.write("{}: {}", MP_DISPATCH( long_name() ),
                 "Alternative solution");
    if (MP_DISPATCH( NumberOfObjectives() ) > 0) {
      writer.write("; objective {}",
                   MP_DISPATCH( FormatObjValue(obj_value) ));
    }
    writer.write("\n");
    auto sol = solution.move_or_copy();
    if (round() && MP_DISPATCH(IsMIP()))
      RoundSolution(sol, writer);
    HandleFeasibleSolution(writer.c_str(),
                   sol.empty() ? 0 : sol.data(),
                   dual_solution.empty() ? 0 : dual_solution.data(),
                   obj_value);
  }

  void ReportSolution() {
    double obj_value = std::numeric_limits<double>::quiet_NaN();
    
    fmt::MemoryWriter writer;
    writer.write("{}: {}", MP_DISPATCH( long_name() ), solve_status_);
    if (solve_code_ < sol::INFEASIBLE) {
      if (MP_DISPATCH( NumberOfObjectives() ) > 0) {
        if(multiobj() && MP_DISPATCH(NumberOfObjectives()) > 1)
        {
          auto obj_values = MP_DISPATCH(ObjectiveValues());
          writer.write("; objective {}", MP_DISPATCH(FormatObjValue(obj_values[0])));
          writer.write("\nIndividual objective values:");
          for (size_t i = 0; i < obj_values.size(); i++)
            writer.write("\n\t_sobj[{}] = {}", i+1, // indexing of _sobj starts from 1
              MP_DISPATCH(FormatObjValue(obj_values[i])));
        }
        else {
          obj_value = MP_DISPATCH(ObjectiveValue());
          writer.write("; ");
          if (feasrelax_IOdata())
            writer.write("feasrelax ");
          writer.write("objective {}",
            MP_DISPATCH(FormatObjValue(obj_value)));
          if (feasrelax_IOdata().origObjAvailable_)
            writer.write("\nOriginal objective = {}",
                         feasrelax_IOdata().origObjValue_);
        }
      }
    }
    if (exportKappa() && 1)
      writer.write("\nkappa value: {}", MP_DISPATCH(Kappa()));
    if (auto ni = MP_DISPATCH( NumberOfIterations() ))
      writer.write("\n{} simplex iterations", ni);
    if (auto nnd = MP_DISPATCH( NodeCount() ))
      writer.write("\n{} branching nodes", nnd);
    writer.write("\n");
    if (solver_msg_extra_.size()) {
      writer.write(solver_msg_extra_);
    }
    auto sol = MP_DISPATCH( PrimalSolution() ).move_or_copy();
    if (round() && MP_DISPATCH(IsMIP()))
      RoundSolution(sol, writer);
    auto dual_solution = MP_DISPATCH( DualSolution() );  // Try in any case
    HandleSolution(solve_code_, writer.c_str(),
                   sol.empty() ? 0 : sol.data(),
                   dual_solution.empty() ? 0 : dual_solution.data(), obj_value);
  }

  void Abort(int /*solve_code_now*/, std::string msg) {
    /// TODO: need a SolutionHandler in Converter even before NL
    /// - for example, when cloud env fails
//    HandleSolution(solve_code_now, msg, 0, 0, 0.0);
    MP_RAISE(msg);
  }

  void PrintTimingInfo() {
    double output_time = GetTimeAndReset(stats.time);
    MP_DISPATCH( Print("Setup time = {:.6f}s\n"
                       "Solution time = {:.6f}s\n"
                       "Output time = {:.6f}s\n",
                       stats.setup_time, stats.solution_time, output_time) );
  }

  void RoundSolution(std::vector<double>& sol,
                     fmt::MemoryWriter& writer) {
    auto rndres = DoRound(sol);
    if (rndres.first) {
      ModifySolveCodeAndMessageAfterRounding(rndres, writer);
    }
  }

  std::pair<int, double> DoRound(std::vector<double>& sol) {
    int nround = 0;
    double maxmodif = 0.0;
    const bool fAssign = round() & 1;
    const auto& fInt = IsVarInt();
    for (auto j = std::min(fInt.size(), sol.size()); j--; ) {
      if (fInt[j]) {
        auto y = std::round(sol[j]);
        auto d = std::fabs(sol[j]-y);
        if (0.0!=d) {
          ++nround;
          if (maxmodif < d)
            maxmodif = d;
          if (fAssign)
            sol[j] = y;
        }
      }
    }
    return {nround, maxmodif};
  }

  void ModifySolveCodeAndMessageAfterRounding(
        std::pair<int, double> rndres, fmt::MemoryWriter& writer) {
    if (round() & 2 && IsSolStatusRetrieved()) {
      solve_code_ = 3 - (round() & 1);
    }
    if (round() & 4) {
      auto sc = rndres.first > 1 ? "s" : "";
      writer.write(
            "\n{} integer variable{} {}rounded to integer{};"
      " maxerr = {:.16}", rndres.first, sc,
            round() & 1 ? "" : "would be ", sc, rndres.second);
    }
  }

  //////////////////////// SOLUTION STATUS ADAPTERS ///////////////////////////////
  /** Following the taxonomy of the enum sol, returns true if
      we have an optimal solution or a feasible solution for a 
      satisfaction problem */
  bool IsProblemSolved() const {
    assert(IsSolStatusRetrieved());
    return solve_code_ == sol::SOLVED;

  }
  bool IsProblemInfOrUnb() const {
    assert( IsSolStatusRetrieved() );
    return sol::INFEASIBLE<=solve_code_ &&
        sol::UNBOUNDED>=solve_code_;
  }

  bool IsProblemInfeasible() const {
    assert( IsSolStatusRetrieved() );
    return sol::INFEASIBLE<=solve_code_ &&
        sol::UNBOUNDED>solve_code_;
  }

  bool IsProblemUnbounded() const {
    assert( IsSolStatusRetrieved() );
    return sol::INFEASIBLE<solve_code_ &&
        sol::UNBOUNDED>=solve_code_;
  }

  bool IsSolStatusRetrieved() const {
    return sol::NOT_CHECKED!=solve_code_;
  }

  struct Stats {
    steady_clock::time_point time;
    double setup_time;
    double solution_time;
  };
  Stats stats;


  /////////////////////////////// SOME MATHS ////////////////////////////////
  bool IsFinite(double n) const {
    return n>MP_DISPATCH( MinusInfinity() ) &&
        n<MP_DISPATCH( Infinity() );
  }

  static constexpr double Infinity()
  { return std::numeric_limits<double>::infinity(); }

  static constexpr double MinusInfinity() { return -Infinity(); }


public:
  using Solver::Print;
  using Solver::add_to_long_name;
  using Solver::add_to_version;
  using Solver::set_option_header;
  using Solver::add_to_option_header;

protected:
  void HandleSolution(int status, fmt::CStringRef msg,
      const double *x, const double *y, double obj) {
    GetCQ().HandleSolution(status, msg, x, y, obj);
  }

  void HandleFeasibleSolution(fmt::CStringRef msg,
      const double *x, const double *y, double obj) {
    GetCQ().HandleFeasibleSolution(msg, x, y, obj);
  }

  /// Variables' initial values
  ArrayRef<double> InitialValues() {
    return GetCQ().InitialValues();
  }

  /// Initial dual values
  ArrayRef<double> InitialDualValues() {
    return GetCQ().InitialDualValues();
  }


  template <class N>
  ArrayRef<N> ReadSuffix(const SuffixDef<N>& suf) {
    return GetCQ().ReadSuffix(suf);
  }

  ArrayRef<int> ReadIntSuffix(const SuffixDef<int>& suf) {
    return GetCQ().ReadSuffix(suf);
  }

  ArrayRef<double> ReadDblSuffix(const SuffixDef<double>& suf) {
    return GetCQ().ReadSuffix(suf);
  }

  /// Record suffix values which are written into .sol
  /// by HandleSolution()
  /// Does nothing if vector empty
  void ReportSuffix(const SuffixDef<int>& suf,
                    ArrayRef<int> values) {
    GetCQ().ReportSuffix(suf, values);
  }
  void ReportSuffix(const SuffixDef<double>& suf,
                    ArrayRef<double> values) {
    GetCQ().ReportSuffix(suf, values);
  }
  void ReportIntSuffix(const SuffixDef<int>& suf,
                       ArrayRef<int> values) {
    GetCQ().ReportSuffix(suf, values);
  }
  void ReportDblSuffix(const SuffixDef<double>& suf,
                       ArrayRef<double> values) {
    GetCQ().ReportSuffix(suf, values);
  }

  template <class N>
  void ReportSingleSuffix(const SuffixDef<N>& suf,
                          N value) {
    std::vector<N> values(1, value);
    GetCQ().ReportSuffix(suf, values);
  }

  /// Access underlying model instance
  const std::vector<bool>& IsVarInt() const {
    return GetCQ().IsVarInt();
  }

  ///////////////////////// STORING SOLUTON STATUS //////////////////////
private:
  int solve_code_=sol::NOT_CHECKED;
  std::string solve_status_;

  ///////////////////////// STORING SOLVER MESSAGES //////////////////////
private:
  std::string solver_msg_extra_;
protected:
  void AddToSolverMessage(const std::string& msg)
  { solver_msg_extra_ += msg; }

  ///////////////////////////// OPTIONS /////////////////////////////////
protected:
  template <class Value>
  class StoredOption : public mp::TypedSolverOption<Value> {
    Value& value_;
  public:
    using value_type = Value;
    StoredOption(const char *name_list, const char *description,
                 Value& v, ValueArrayRef values = ValueArrayRef())
      : mp::TypedSolverOption<Value>(name_list, description, values), value_(v) {}

    void GetValue(Value &v) const override { v = value_; }
    void SetValue(typename internal::OptionHelper<Value>::Arg v) override
    { value_ = v; }
  };


private:
  /// Recorded solver options
  using SlvOptionRecord = std::function<void(void)>;
  std::vector< SlvOptionRecord > slvOptionRecords_;

protected:
  void RecordSolverOption(SlvOptionRecord sor)
  { slvOptionRecords_.push_back(sor); }
  void ReplaySolverOptions() {
    for (auto f: slvOptionRecords_)
      f();
  }


  /// Solver options accessor, facilitates calling
  /// backend_.Get/SetSolverOption()
  template <class Value, class Index>
  class SolverOptionAccessor {
    using Backend = Impl;
    Backend& backend_;
  public:
    using value_type = Value;
    using index_type = Index;
    SolverOptionAccessor(Backend& b) : backend_(b) { }
    /// Options setup
    Value get(const SolverOption& , Index i) const {
      Value v;
      backend_.GetSolverOption(i, v);
      return v;
    }
    void set(const SolverOption& ,
             typename internal::OptionHelper<Value>::Arg v,
             Index i) {
      auto *pBackend = &backend_;
      auto setter = [=]() { pBackend->SetSolverOption(i, v); };
      setter();    // run it now
      backend_.RecordSolverOption(setter);
    }
  };

  template <class ValueType, class KeyType>
  class ConcreteOptionWrapper :
      public Solver::ConcreteOptionWithInfo<
      SolverOptionAccessor<ValueType, KeyType>, ValueType, KeyType> {

    using COType = Solver::ConcreteOptionWithInfo<
    SolverOptionAccessor<ValueType, KeyType>, ValueType, KeyType>;
    using SOAType = SolverOptionAccessor<ValueType, KeyType>;

    SOAType soa_;
  public:
    ConcreteOptionWrapper(Impl* impl_, const char *name, const char *description,
                          KeyType k, ValueArrayRef values = ValueArrayRef()) :
      COType(name, description, &soa_, &SOAType::get, &SOAType::set, k, values),
      soa_(*impl_)
    { }
  };

public:
  using Solver::AddOption;
  using Solver::AddOptionSynonymsFront;
  using Solver::AddOptionSynonymsBack;
  using Solver::AddOptionSynonym_OutOfLine;
  using Solver::FindOption;


  /// Simple stored option referencing a variable
  template <class Value>
  void AddStoredOption(const char *name, const char *description,
                       Value& value, ValueArrayRef values = ValueArrayRef()) {
    AddOption(Solver::OptionPtr(
                new StoredOption<Value>(
                  name, description, value, values)));
  }

  /// Simple stored option referencing a variable, min, max (TODO)
  template <class Value>
  void AddStoredOption(const char *name, const char *description,
                       Value& value, Value , Value ) {
    AddOption(Solver::OptionPtr(
                new StoredOption<Value>(
                  name, description, value, ValueArrayRef())));
  }

  /// Adding solver options of types int/double/string/...
  /// The type is deduced from the two last parameters min, max
  /// (currently unused otherwise - TODO)
  /// If min/max omitted, assume ValueType=std::string
  /// Assumes existence of Impl::Get/SetSolverOption(KeyType, ValueType(&))
  template <class KeyType, class ValueType=std::string>
  void AddSolverOption(const char *name_list, const char *description,
                       KeyType k,
                       ValueType , ValueType ) {
    AddOption(Solver::OptionPtr(
                new ConcreteOptionWrapper<
                ValueType, KeyType>(
                  (Impl*)this, name_list, description, k)));
  }

  template <class KeyType, class ValueType = std::string>
  void AddSolverOption(const char* name_list, const char* description,
    KeyType k) {
    AddOption(Solver::OptionPtr(
      new ConcreteOptionWrapper<
      ValueType, KeyType>(
        (Impl*)this, name_list, description, k)));
  }

  /// TODO use vmin/vmax or rely on solver raising error?
  /// TODO also with ValueTable, deduce type from it
  template <class KeyType, class ValueType = std::string>
  void AddSolverOption(const char* name_list, const char* description,
      KeyType k, ValueArrayRef values, ValueType defaultValue) {
    internal::Unused(defaultValue);
    AddOption(Solver::OptionPtr(
      new ConcreteOptionWrapper<
      ValueType, KeyType>(
        (Impl*)this, name_list, description, k, values)));
  }

  void ReplaceOptionDescription(const char* name, const char* desc) {
    auto pOption = FindOption(name);
    assert(pOption);
    pOption->set_description(desc);
  }

  void AddToOptionDescription(const char* name, const char* desc_add) {
    auto pOption = FindOption(name);
    assert(pOption);
    std::string to_add = "\n\n";
    to_add += desc_add;
    pOption->add_to_description(to_add.c_str());
  }


private:
  struct Options {
    int exportKappa_ = 0;

    int feasRelax_=0;
    double lbpen_=1.0, ubpen_=1.0, rhspen_=1.0;

    int round_=0;
    double round_reptol_=1e-9;
  } storedOptions_;

  /// Once Impl allows FEASRELAX,
  /// it should check this via feasrelax_IOdata()
  struct FeasrelaxIO {
    /// Whether feasrelax should be done
    operator bool() const { return mode_; }
    /// --------------------- INPUT ----------------------
    int mode_=0;  // whether Impl should do it and which mode,
                  // can be redefined by Impl if cannot map
                  // from standard options
    int mode() const { return mode_; }
    /// Empty vector means +inf penalties
    std::vector<double> lbpen, ubpen, rhspen;

    /// --------------------- OUTPUT, filled by Impl -----
    bool origObjAvailable_ = false;
    double origObjValue_ = 0.0;
  } feasRelaxIO_;


protected:  //////////// Option accessors ////////////////
  int exportKappa() const { return storedOptions_.exportKappa_; }

  /// Feasrelax I/O data
  FeasrelaxIO& feasrelax_IOdata() { return feasRelaxIO_; }
  int feasrelaxMode() const { return storedOptions_.feasRelax_; }
  double lbpen() const { return storedOptions_.lbpen_; }
  double ubpen() const { return storedOptions_.ubpen_; }
  double rhspen() const { return storedOptions_.rhspen_; }

  /// Whether to round MIP solution and modify messages
  int round() const
  { return IMPL_HAS_STD_FEATURE(WANT_ROUNDING) ? storedOptions_.round_ : 0; }
  /// MIP solution rounding reporting tolerance
  double round_reptol() const { return storedOptions_.round_reptol_; }


protected:
  void InitStandardOptions() {
    if (IMPL_HAS_STD_FEATURE(KAPPA))
      AddStoredOption("alg:kappa kappa basis_cond",  
        "Whether to return the estimated condition number (kappa) of "
        "the optimal basis (default 0): sum of 1 = report kappa in the result message; "
        "2 = return kappa in the solver-defined suffix kappa on the objective and "
        "problem. The request is ignored when there is no optimal basis.",
        storedOptions_.exportKappa_);

    if (IMPL_HAS_STD_FEATURE(FEAS_RELAX)) {
      AddStoredOption("alg:feasrelax feasrelax",
                      "Whether to modify the problem into a feasibility "
                          "relaxation problem:\n"
                          "\n"
                          "| 0 = no (default)\n"
                          "| 1 = yes, minimizing the weighted sum of violations\n"
                          "| 2 = yes, minimizing the weighted sum of squared violations\n"
                          "| 3 = yes, minimizing the weighted count of violations\n"
                          "| 4-6 = same objective as 1-3, but also optimize the "
                             "original objective, subject to the violation "
                             "objective being minimized.\n"
                      "\n"
                      "Weights are given by suffixes .lbpen and .ubpen on variables "
                      "and .rhspen on constraints (when nonnegative), else by keywords "
                      "alg:lbpen, alg:ubpen, and alg:rhspen, respectively (default values = 1). "
                      "Weights < 0 are treated as Infinity, allowing no violation.",
          storedOptions_.feasRelax_);
      AddStoredOption("alg:lbpen lbpen", "See alg:feasrelax.",
          storedOptions_.lbpen_);
      AddStoredOption("alg:ubpen ubpen", "See alg:feasrelax.",
          storedOptions_.ubpen_);
      AddStoredOption("alg:rhspen rhspen", "See alg:feasrelax.",
          storedOptions_.rhspen_);
    }

    if (IMPL_HAS_STD_FEATURE( WANT_ROUNDING )) {
      AddStoredOption("mip:round round",
                      "Whether to round integer variables to integral values before "
                      "returning the solution, and whether to report that the solver "
                      "returned noninteger values for integer values:  sum of\n"
                      "\n"
                      "|  1 ==> round nonintegral integer variables\n"
                      "|  2 ==> modify solve_result\n"
                      "|  4 ==> modify solve_message\n"
                      "\n"
                      "Default = 0.  Modifications that were or would be made are "
                      "reported in solve_result and solve_message only if the maximum "
                      "deviation from integrality exceeded mip:round_reptol.",
                    storedOptions_.round_);
      AddStoredOption("mip:round_reptol round_reptol",
                      "Tolerance for reporting rounding of integer variables to "
                      "integer values; see \"mip:round\".  Default = 1e-9.",
                    storedOptions_.round_reptol_);
    }

  }

  void InitCustomOptions() { }


  //////////////////////////////////////////////////////////////////////////////
  //////////////////////////// STANDARD SUFFIXES ///////////////////////////////
  //////////////////////////////////////////////////////////////////////////////
private:
  const SuffixDef<int> suf_objpriority = { "objpriority", suf::OBJ | suf::INPUT };
  const SuffixDef<double> suf_objweight = { "objweight", suf::OBJ | suf::INPUT };
  const SuffixDef<double> suf_objabstol = { "objabstol", suf::OBJ | suf::INPUT };
  const SuffixDef<double> suf_objreltol = { "objreltol", suf::OBJ | suf::INPUT };
  
  const SuffixDef<double> suf_objkappa = { "kappa", suf::OBJ | suf::OUTONLY };
  const SuffixDef<double> suf_probkappa = { "kappa", suf::PROBLEM | suf::OUTONLY };


  /////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////// SERVICE STUFF ///////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////
public:
  /// Default mp::Solver flags,
  /// used there to implement multiobj and .nsol
  static int Flags() {
    int flg=0;
    if ( IMPL_HAS_STD_FEATURE(MULTISOL) )
      flg |= Solver::MULTIPLE_SOL;
    if ( IMPL_HAS_STD_FEATURE(MULTIOBJ) )
      flg |= Solver::MULTIPLE_OBJ;
    return flg;
  }

  void InitMetaInfoAndOptions() {
    MP_DISPATCH( InitNamesAndVersion() );
    MP_DISPATCH( InitStandardOptions() );
    MP_DISPATCH( InitCustomOptions() );
  }

  void InitNamesAndVersion() {
    auto name = MP_DISPATCH( GetSolverName() );
    auto version = MP_DISPATCH( GetSolverVersion() );
    this->set_long_name( fmt::format("{} {}", name, version ) );
    this->set_version( fmt::format("AMPL/{} Optimizer [{}]",
                                   name, version ) );
  }

  /// Converter should provide this before Backend can run solving
  void ProvideConverterQueryObject(ConverterQuery* pCQ)
  { p_converter_query_object = pCQ; }

private: // hiding this detail, it's not for the final backends
  const ConverterQuery& GetCQ() const {
    assert(nullptr!=p_converter_query_object);
    return *p_converter_query_object;
  }
  ConverterQuery& GetCQ() {
    assert(nullptr!=p_converter_query_object);
    return *p_converter_query_object;
  }
  ConverterQuery *p_converter_query_object = nullptr;
  using MPSolverBase = SolverImpl< ModelAdapter< BasicModel<> > >;
public:
  using MPUtils = MPSolverBase;              // Allow Converter access the SolverImpl
  const MPUtils& GetMPUtils() const { return *this; }
  MPUtils& GetMPUtils() { return *this; }

  using MPSolverBase::debug_mode;

protected:
  /// Returns {} if these penalties are +inf
  std::vector<double> FillFeasRelaxPenalty(
      ArrayRef<double> suf_pen, double pen, int n) {
    if (suf_pen.empty() && pen<0.0)
      return {};
    std::vector<double> result(n,
                               pen<0.0 ? MP_DISPATCH(Infinity()) : pen);
    for (size_t i=suf_pen.size(); i--; ) {
      result[i] = suf_pen[i]<0.0 ? MP_DISPATCH(Infinity()) : suf_pen[i];
    }
    return result;
  }

  /// Convenience method
  /// Gurobi reports duals separately for linear and QCP constraints
  /// We rely on QCP ones coming first in NL
  static
      std::vector<double> MakeDualsFromLPAndQCPDuals(
        std::vector<double> pi, std::vector<double> qcpi) {
    qcpi.insert(qcpi.end(), pi.begin(), pi.end());
    return qcpi;
  }

  /// Gurobi handles linear constraints as a separate class.
  /// AMPL provides suffixes for all constraints together.
  /// The method returns the indexes of linear constraints
  /// which have suffixes, in the overall constraints list.
  const std::vector<size_t>& GetIndexesOfLinearConstraintsWithSuffixes() const
  { return orig_lin_constr_; }

private:
  /// Indices of NL original linear constr in the total constr ordering
  std::vector<size_t> orig_lin_constr_;
  size_t n_alg_constr_=0;

public:
  BasicBackend() :
    MPSolverBase(
      Impl::GetSolverInvocationName(),
      Impl::GetAMPLSolverLongName(),
      Impl::Date(), Impl::Flags())
  { }
  virtual ~BasicBackend() { }
};

}  // namespace mp

#endif  // BACKEND_H_
