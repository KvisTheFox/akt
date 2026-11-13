const std = @import("std");
pub fn main() !void {
    var sum: i64 = 0;
    var i: i64 = 0;
    while (i < 100000000) : (i += 1) sum += @mod(i * 3 + 1, 7);
    try std.io.getStdOut().writer().print("loop {}\n", .{sum});
}
