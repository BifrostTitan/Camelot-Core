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
using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using DOL.Database;
using DOL.GS;
using DOL.GS.Geometry;
using DOL.Tests;

AssemblyLoadContext.Default.Resolving += (context,name) =>
{
    var path=Path.Combine(AppContext.BaseDirectory,name.Name+".dll");
    return File.Exists(path) ? context.LoadFromAssemblyPath(path) : null;
};
if (args.Contains("--writes"))
{
    CacheTimestampWrites.Run();
    return;
}
var update=typeof(WorldUpdateThread).GetMethod("UpdatePlayerWorld",BindingFlags.NonPublic|BindingFlags.Static,null,new[] {typeof(GamePlayer),typeof(long)},null)!.CreateDelegate<Action<GamePlayer,long>>();
DOL.GS.ServerProperties.Properties.WORLD_PLAYERTOPLAYER_UPDATE_INTERVAL=1000;
DOL.GS.ServerProperties.Properties.WORLD_NPC_UPDATE_INTERVAL=8000;
DOL.GS.ServerProperties.Properties.WORLD_OBJECT_UPDATE_INTERVAL=30000;
const long now=1000000;
var samples=new List<object>();
foreach(int count in new[] {100,1000,1500})
foreach(int agedPercent in new[] {0,5,100})
{
    var client=new GameClient(null);
    long sends=0;
    client.Out=new TestPacketLib {SendPlayerForgedPositionMethod=(_,p)=>sends++};
    var players=Enumerable.Range(0,count).Select(i=>new Player(null) {ObjectID=i+1}).ToArray();
    var region=new TestRegion(players.Cast<GameObject>().ToArray());
    var observer=new Player(client) {Neighbors=players};
    foreach(var p in players) client.GameObjectUpdateArray.Add(Tuple.Create(region.ID,(ushort)p.ObjectID),p.ObjectID<=count*agedPercent/100 ? 0:now);
    WorldMgr.Regions.Add(region.ID,region);
    try
    {
        for(int i=0;i<100;i++) update(observer,now);
        for(int round=0;round<5;round++)
        {
            GC.Collect();GC.WaitForPendingFinalizers();
            long beforeSends=sends;
            long bytes=GC.GetAllocatedBytesForCurrentThread();
            var watch=Stopwatch.StartNew();
            for(int i=0;i<1000;i++) update(observer,now);
            watch.Stop();bytes=GC.GetAllocatedBytesForCurrentThread()-bytes;
            long expected=(long)(count*agedPercent/100)*1000;
            if(sends-beforeSends!=expected) throw new Exception("Update count changed");
            samples.Add(new {count,agedPercent,round,updates=1000,milliseconds=watch.Elapsed.TotalMilliseconds,allocatedBytes=bytes,sends=sends-beforeSends});
        }
    }
    finally {WorldMgr.Regions.Remove(region.ID);}
}
Console.WriteLine(JsonSerializer.Serialize(samples,new JsonSerializerOptions {WriteIndented=true}));
class TestRegion : Region
{
    public TestRegion(GameObject[] objects):base(null,new RegionData {Id=64995}) {m_objects=objects;}
}
class Player : GamePlayer
{
    public IEnumerable Neighbors=Array.Empty<GamePlayer>();
    public Player(GameClient client):base(client,new DOLCharacters()) {}
    public override void LoadFromDatabase(DataObject obj) {}
    public override ushort CurrentRegionID {get=>64995;set{}}
    // Empty spatial results isolate cache work from radius-query performance.
    public override Region CurrentRegion {get=>null;set{}}
    public override bool IsVisibleTo(GameObject observer)=>true;
    public override bool IsStealthed=>false;
    public override IEnumerable GetPlayersInRadius(ushort radius)=>Neighbors;
}
