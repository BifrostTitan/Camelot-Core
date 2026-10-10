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
using System.Text.Json;
using System.Runtime.Loader;
using DOL.Database;
using DOL.GS;
using DOL.GS.Geometry;

AssemblyLoadContext.Default.Resolving += (context,name) =>
{
    var path=Path.Combine(AppContext.BaseDirectory,name.Name+".dll");
    return File.Exists(path) ? context.LoadFromAssemblyPath(path) : null;
};
var samples = new List<object>();
foreach (int count in new[] {10,100,1000,1500})
{
    var region = new Region(null,new RegionData { Id=64998 });
    var zone = new Zone(region,64998,"Performance audit",0,0,65536,65536,64998,false,0,false,0,0,0,0,0);
    for (int i=0;i<count;++i)
    {
        var player = new AuditPlayer(region) { ObjectID=i+1,
            Position=Position.Create(64998,10000+i%20*5,10000+i/20*5,0), ObjectState=GameObject.eObjectState.Active };
        zone.ObjectEnterZone(player);
    }
    var method=typeof(Zone).GetMethod("GetObjectsInRadius",BindingFlags.Instance|BindingFlags.NonPublic,null,
        new[] {typeof(Zone.eGameObjectType),typeof(Coordinate),typeof(ushort),typeof(ArrayList),typeof(bool)},null)!;
    var query=method.CreateDelegate<RadiusQuery>(zone);
    foreach (bool wholeCell in new[] {false,true})
    {
        var center=Coordinate.Create(wholeCell ? 11000 : 10000,10000,0);
        ArrayList Run() => query(Zone.eGameObjectType.PLAYER,center,4096,new ArrayList(),false);
        for(int i=0;i<100;++i) if(Run().Count!=count) throw new Exception("Incomplete crowd query");
        for(int round=0;round<5;++round)
        {
            GC.Collect();GC.WaitForPendingFinalizers();
            long allocated=GC.GetAllocatedBytesForCurrentThread();
            var watch=Stopwatch.StartNew();
            for(int i=0;i<1000;++i) if(Run().Count!=count) throw new Exception("Incomplete crowd query");
            watch.Stop();allocated=GC.GetAllocatedBytesForCurrentThread()-allocated;
            samples.Add(new {count,wholeCell,round,queries=1000,milliseconds=watch.Elapsed.TotalMilliseconds,allocatedBytes=allocated});
        }
    }
    zone.Delete();
}
Console.WriteLine(JsonSerializer.Serialize(samples,new JsonSerializerOptions {WriteIndented=true}));

delegate ArrayList RadiusQuery(Zone.eGameObjectType type,Coordinate center,ushort radius,ArrayList partial,bool ignoreZ);
class AuditPlayer : GamePlayer
{
    readonly Region region;
    public AuditPlayer(Region region) : base(null,new DOLCharacters()) { this.region=region; }
    public override Region CurrentRegion {get=>region;set{}}
    public override void LoadFromDatabase(DataObject obj) {}
}
