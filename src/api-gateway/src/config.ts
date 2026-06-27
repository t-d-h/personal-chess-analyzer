export const RATE_LIMIT_MAX = parseInt(process.env["RATE_LIMIT_MAX"] ?? "10", 10);
export const RATE_LIMIT_WINDOW = parseInt(process.env["RATE_LIMIT_WINDOW"] ?? "60000", 10);
