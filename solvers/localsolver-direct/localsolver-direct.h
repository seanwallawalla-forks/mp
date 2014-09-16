/*
 AMPL solver interface to LocalSolver.

 Copyright (C) 2014 AMPL Optimization Inc

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

 Author: Victor Zverovich
 */

#ifndef MP_SOLVERS_LOCALSOLVER_H_
#define MP_SOLVERS_LOCALSOLVER_H_

#include <localsolver.h>
#include "asl/expr.h"
#include "mp/problem-builder.h"
#include "mp/solver.h"

namespace mp {

namespace ls = localsolver;

// Converter of optimization problems from NL to LocalSolver format.
/*class NLToLocalSolverConverter :
  public ExprConverter<NLToLocalSolverConverter, ls::LSExpression> {
 private:
  ls::LSModel &model_;
  std::vector<ls::LSExpression> vars_;

public:
  ls::LSExpression VisitRem(BinaryExpr e) {
    return ConvertBinary(ls::O_Mod, e);
  }
  ls::LSExpression VisitNumericLess(BinaryExpr e) {
    return model_.createExpression(ls::O_Max,
        ConvertBinary(ls::O_Sub, e), MakeConst(0));
  }
  ls::LSExpression VisitIntDiv(BinaryExpr e) {
    ls::LSExpression rem = VisitRem(e);
    return model_.createExpression(ls::O_Div,
        model_.createExpression(ls::O_Sub, rem.getOperand(0), rem),
        rem.getOperand(1));
  }

  // TODO
  /*ls::LSExpression VisitRound(BinaryExpr e) {
    // round does nothing because Gecode supports only integer expressions.
    RequireZeroRHS(e, "round");
    return Visit(e.lhs());
  }

  ls::LSExpression VisitTrunc(BinaryExpr e) {
    // trunc does nothing because Gecode supports only integer expressions.
    RequireZeroRHS(e, "trunc");
    return Visit(e.lhs());
  }

  ls::LSExpression VisitCount(CountExpr e);

  ls::LSExpression VisitNumberOf(NumberOfExpr e);

  ls::LSExpression VisitNot(NotExpr e) {
    return ConvertUnary(ls::O_Not, e);
  }

  ls::LSExpression VisitOr(BinaryLogicalExpr e) {
    return ConvertBinary(ls::O_Or, e);
  }

  ls::LSExpression VisitAnd(BinaryLogicalExpr e) {
    return ConvertBinary(ls::O_And, e);
  }

  ls::LSExpression VisitIff(BinaryLogicalExpr e) {
    return ConvertBinary(ls::O_Eq, e);
  }

  ls::LSExpression VisitLess(RelationalExpr e) {
    return ConvertBinary(ls::O_Lt, e);
  }

  ls::LSExpression VisitLessEqual(RelationalExpr e) {
    return ConvertBinary(ls::O_Leq, e);
  }

  ls::LSExpression VisitEqual(RelationalExpr e) {
    return ConvertBinary(ls::O_Eq, e);
  }

  ls::LSExpression VisitGreaterEqual(RelationalExpr e) {
    return ConvertBinary(ls::O_Geq, e);
  }

  ls::LSExpression VisitGreater(RelationalExpr e) {
    return ConvertBinary(ls::O_Gt, e);
  }

  ls::LSExpression VisitNotEqual(RelationalExpr e) {
    return ConvertBinary(ls::O_Neq, e);
  }

  ls::LSExpression VisitForAll(IteratedLogicalExpr e) {
    return ConvertVarArg(ls::O_And, e);
  }

  ls::LSExpression VisitExists(IteratedLogicalExpr e) {
    return ConvertVarArg(ls::O_Or, e);
  }

  ls::LSExpression VisitImplication(ImplicationExpr e) {
    return VisitIf(e);
  }

  ls::LSExpression VisitAllDiff(AllDiffExpr e);

  ls::LSExpression VisitLogicalConstant(LogicalConstant c) {
    ls::lsint value = c.value();
    return model_.createConstant(value);
  }
};*/

class LocalSolver;

// This class provides methods for building a problem in LocalSolver format.
class LSProblemBuilder :
    public ProblemBuilder<LSProblemBuilder, ls::LSExpression> {
 private:
  ls::LSModel model_;
  int num_continuous_vars_;

  std::vector<ls::LSExpression> vars_;

  struct ObjInfo {
    ls::LSObjectiveDirection direction;
    ls::LSExpression expr;
    ObjInfo() : direction(ls::OD_Minimize) {}
  };
  std::vector<ObjInfo> objs_;

  struct ConInfo {
    ls::LSExpression expr;
    double lb, ub;
    ConInfo() : lb(0), ub(0) {}
  };
  std::vector<ConInfo> cons_;

  typedef ProblemBuilder<LSProblemBuilder, ls::LSExpression> Base;

  static ls::lsint MakeInt(int value) { return value; }

  ls::lsint ConvertToInt(double value) {
    // TODO
    return static_cast<ls::lsint>(value);
  }

  ls::LSExpression Negate(ls::LSExpression arg) {
    return model_.createExpression(ls::O_Sub, MakeInt(0), arg);
  }

 public:
  explicit LSProblemBuilder(LocalSolver &solver);

  int num_vars() const { return vars_.size(); }
  int num_continuous_vars() const { return num_continuous_vars_; }
  int num_objs() const { return objs_.size(); }

  // TODO

  void BeginBuild(const NLHeader &header);
  void EndBuild();

  void SetObj(int index, obj::Type type, ls::LSExpression expr) {
    ObjInfo &obj_info = objs_[index];
    if (type == obj::MAX)
      obj_info.direction = ls::OD_Maximize;
    obj_info.expr = expr;
  }

  void SetCon(int index, ls::LSExpression expr) { cons_[index].expr = expr; }

  class LinearExprHandler {
   private:
    LSProblemBuilder &builder_;
    ls::LSExpression expr_;

   public:
    LinearExprHandler(LSProblemBuilder &builder,
                      ls::LSExpression &expr, ls::LSExpression sum)
      : builder_(builder), expr_(sum) {
      if (expr != ls::LSExpression()) {
        // Add nonlinear expression.
        sum.addOperand(expr);
        expr = sum;
      } else {
        expr = sum;
      }
    }

    void AddTerm(int var_index, double coef) {
      expr_.addOperand(builder_.model_.createExpression(
                         ls::O_Prod, coef, builder_.vars_[var_index]));
    }
  };

  typedef LinearExprHandler LinearObjHandler;

  LinearObjHandler GetLinearObjHandler(int obj_index, int) {
    return LinearObjHandler(
          *this, objs_[obj_index].expr, model_.createExpression(ls::O_Sum));
  }

  typedef LinearExprHandler LinearConHandler;

  LinearConHandler GetLinearConHandler(int con_index, int) {
    return LinearObjHandler(
          *this, cons_[con_index].expr, model_.createExpression(ls::O_Sum));
  }

  void SetVarBounds(int index, double lb, double ub) {
    ls::LSExpression var = vars_[index];
    if (index < num_continuous_vars_) {
      var.addOperand(lb);
      var.addOperand(ub);
    } else {
      var.addOperand(ConvertToInt(lb));
      var.addOperand(ConvertToInt(ub));
    }
  }

  void SetConBounds(int index, double lb, double ub) {
    ConInfo &con = cons_[index];
    con.lb = lb;
    con.ub = ub;
  }

  // Ignore Jacobian column sizes.
  ColumnSizeHandler GetColumnSizeHandler() { return ColumnSizeHandler(); }

  ls::LSExpression MakeNumericConstant(double value) {
    return model_.createConstant(value);
  }

  ls::LSExpression MakeVariable(int var_index) { return vars_[var_index]; }

  ls::LSExpression MakeUnary(expr::Kind kind, ls::LSExpression arg);
  ls::LSExpression MakeBinary(
      expr::Kind kind, ls::LSExpression lhs, ls::LSExpression rhs);

  ls::LSExpression MakeIf(ls::LSExpression condition,
      ls::LSExpression true_expr, ls::LSExpression false_expr) {
    return model_.createExpression(ls::O_If, condition, true_expr, false_expr);
  }

  // LocalSolver doesn't support piecewise-liner terms and functions.

  class NumericArgHandler {
   private:
    ls::LSExpression expr_;

   public:
    explicit NumericArgHandler(ls::LSExpression expr) : expr_(expr) {}

    ls::LSExpression expr() const { return expr_; }

    void AddArg(ls::LSExpression arg) { expr_.addOperand(arg); }
  };

  NumericArgHandler BeginVarArg(expr::Kind kind, int) {
    return NumericArgHandler(
          model_.createExpression(kind == expr::MIN ? ls::O_Min : ls::O_Max));
  }
  ls::LSExpression EndVarArg(NumericArgHandler handler) {
    return handler.expr();
  }

  NumericArgHandler BeginSum(int) {
    return NumericArgHandler(model_.createExpression(ls::O_Sum));
  }
  ls::LSExpression EndSum(NumericArgHandler handler) {
    return handler.expr();
  }

  // TODO: count

  NumericArgHandler BeginNumberOf(int num_args) {
    // TODO
    Base::BeginNumberOf(num_args);
    return NumericArgHandler(ls::LSExpression());
  }
  ls::LSExpression EndNumberOf(NumericArgHandler) {
    // TODO
    return ls::LSExpression();
  }

  // TODO
};

class LocalSolver : public SolverImpl<LSProblemBuilder> {
 private:
  ls::LocalSolver solver_;
  int timelimit_;

  int GetTimeLimit(const SolverOption &) const {
    return timelimit_;
  }

  void SetTimeLimit(const SolverOption &opt, int value) {
    if (value <= 0)
      throw InvalidOptionValue(opt, value);
    timelimit_ = value;
  }

 private:
  void DoSolve(Problem &) {} // TODO

 public:
  LocalSolver();

  ls::LSModel model() { return solver_.getModel(); }

  void Solve(LSProblemBuilder &pb);
};
}  // namespace mp

#endif  // MP_SOLVERS_LOCALSOLVER_H_