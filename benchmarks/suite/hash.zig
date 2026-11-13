const std = @import("std");
const N: usize = 16777216;
pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const a = gpa.allocator();
    const data = try a.alloc(u8, N);
    defer a.free(data);
    for (data, 0..) |*v, i| v.* = @intCast((i * 31 + 7) % 251);
    var h: u64 = 1469598103934665603;
    for (data) |v| {
        h ^= v;
        h = h *% 1099511628211;
    }
    try std.io.getStdOut().writer().print("hash {}\n", .{h % 1000000007});
}
