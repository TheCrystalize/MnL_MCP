/**
 * Auto-generated from api_schema\sympy\sympy.explore.schema.json
 * Schema title: sympy.explore.params
 */

export interface sympyexploreparams {
  expr: string;
  goals: "simplify" | "factor" | "solve" | "expand" | "differentiate" | "integrate" | "subs"[];
  variables?: string[];
  timeout_ms?: number;
  options?: Record<string, any>;
}
