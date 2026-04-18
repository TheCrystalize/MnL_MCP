/**
 * Auto-generated from api_schema\z3\z3.solve.schema.json
 * Schema title: z3.solve.params
 */

export interface z3solveparams {
  smt2: string;
  timeout_ms?: number;
  options?: {
  produce_model?: boolean;
  logic?: string;
  random_seed?: number;
};
}
