/*
 * DAWN OF LIGHT - The first free open source DAoC server emulator
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
using System.Diagnostics;
using System.Text.Json;
using DOL.GS;

// Isolate the cache-write portion of a movement broadcast, without sending packets.
internal static class CacheTimestampWrites
{
    internal static void Run()
    {
        var samples = new List<object>();
        foreach (int count in new[] { 100, 1000, 1500 })
        {
            var recipients = Enumerable.Range(0, count).Select(_ => new GameClient(null)).ToArray();
            foreach (var client in recipients)
                client.GameObjectUpdateArray[Tuple.Create((ushort)1, (ushort)42)] = 0;
            void Broadcast(long stamp)
            {
                foreach (var client in recipients)
                    client.GameObjectUpdateArray[Tuple.Create((ushort)1, (ushort)42)] = stamp;
            }
            for (int i = 0; i < 100; i++) Broadcast(i);
            for (int round = 0; round < 5; round++)
            {
                GC.Collect(); GC.WaitForPendingFinalizers();
                long bytes = GC.GetAllocatedBytesForCurrentThread();
                var watch = Stopwatch.StartNew();
                for (int i = 0; i < 1000; i++) Broadcast(i);
                watch.Stop(); bytes = GC.GetAllocatedBytesForCurrentThread() - bytes;
                foreach (var client in recipients)
                    if (client.GameObjectUpdateArray[Tuple.Create((ushort)1, (ushort)42)] != 999)
                        throw new Exception("Recipient timestamp changed");
                samples.Add(new { count, round, broadcasts = 1000, milliseconds = watch.Elapsed.TotalMilliseconds, allocatedBytes = bytes });
            }
        }
        Console.WriteLine(JsonSerializer.Serialize(samples, new JsonSerializerOptions { WriteIndented = true }));
    }
}
