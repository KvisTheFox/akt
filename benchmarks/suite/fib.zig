const std = @import("std");
fn fib(n: i64) i64 { return if (n < 2) n else fib(n - 1) + fib(n - 2); }
pub fn main() !void {
    const out = std.io.getStdOut().writer();
    try out.print("fib {}\n", .{fib(32)});
}
