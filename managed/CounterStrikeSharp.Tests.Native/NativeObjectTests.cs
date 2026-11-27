using System.Threading.Tasks;
using CounterStrikeSharp.API;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Modules.Utils;
using Xunit;

namespace NativeTestsPlugin;

public class NativeObjectsTests
{
    [Fact]
    public async Task EnsureNativeHandle_IsFreed_Vector3()
    {
        // The tracker frees dead handles in the background (on each Track and on a 1 s timer): free every earlier
        // test's handles first, so the count below sees only this vector.
        await FreeNativeHandlesAsync();
        await Server.NextFrameAsync(() =>
        {
            var vector = new Vector(0, 0, 500);
            Assert.Equal(IntPtr.Zero, vector.RawHandle);
            Assert.Equal(500, NativeAPI.VectorGetZ(vector.Handle));
            Assert.Single(NativeHandleTracker._entries);
        });

        await FreeNativeHandlesAsync();
    }

    private static async Task FreeNativeHandlesAsync()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        await WaitUntilAsync(() => NativeHandleTracker._entries.Count == 0, "every tracked native handle to be freed");
    }
}
