def test_solve_smt_basic():
    from mcp_z3 import solve_smt
    res = solve_smt("(declare-const x Int) (assert (> x 0)) (check-sat)", timeout_ms=1000)
    assert isinstance(res, dict)
    assert "status" in res
