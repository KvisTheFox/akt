const std = @import("std");
const D: usize = 256;
pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const al = gpa.allocator();
    const a = try al.alloc(f64, D * D);
    const b = try al.alloc(f64, D * D);
    const c = try al.alloc(f64, D * D);
    defer al.free(a); defer al.free(b); defer al.free(c);
    for (a, 0..) |*v, i| v.* = @floatFromInt(i % 13);
    for (b, 0..) |*v, i| v.* = @floatFromInt(i % 7);
    @memset(c, 0);
    for (0..D) |r| {
        for (0..D) |k| {
            const av = a[r * D + k];
            for (0..D) |col| c[r * D + col] += av * b[k * D + col];
        }
    }
    var sum: f64 = 0;
    for (c) |v| sum += v;
    try std.io.getStdOut().writer().print("matmul {d:.2}\n", .{sum});
}
