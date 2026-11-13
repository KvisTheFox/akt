const std = @import("std");
const N: usize = 5000000;
pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const a = gpa.allocator();
    const sieve = try a.alloc(u8, N);
    defer a.free(sieve);
    for (sieve) |*v| v.* = 1;
    sieve[0] = 0; sieve[1] = 0;
    var i: usize = 2;
    while (i * i < N) : (i += 1) {
        if (sieve[i] == 1) {
            var j = i * i;
            while (j < N) : (j += i) sieve[j] = 0;
        }
    }
    var count: i64 = 0;
    for (sieve) |v| count += v;
    try std.io.getStdOut().writer().print("sieve {}\n", .{count});
}
