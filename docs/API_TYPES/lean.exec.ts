/**
 * Auto-generated from api_schema\lean\lean.exec.schema.json
 * Schema title: lean.exec.params
 */

export interface leanexecparams {
  source: string;
  mode: "check" | "eval" | "verify" | "build";
  timeout_ms?: number;
  options?: Record<string, any>;
}
