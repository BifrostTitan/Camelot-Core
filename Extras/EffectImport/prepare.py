"""Resolve classic effect tables and decoded NIF emitters into an offline manifest."""
import argparse, collections, csv, hashlib, json, math
from pathlib import Path
from PIL import Image

def rows(path):
    with path.open(encoding='cp1252') as source:
        return [r for r in csv.reader(source) if r and r[0].strip().isdigit()]

def number(row, index, fallback=0):
    try: return int(row[index].strip()) if row[index].strip() else fallback
    except (IndexError, ValueError): return fallback

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client',type=Path,required=True)
    parser.add_argument('--decoded',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--limit-systems',type=int,default=0)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    nifs={f['file'].lower():f for f in json.loads(args.decoded.read_text())}
    nif_table={int(r[0]):r[2].lower()+'.nif' for r in rows(args.client/'spnifs.csv')}
    effect_rows=rows(args.client/'speffects.csv')
    files={p.name.lower():p for p in (args.client/'effects').rglob('*') if p.is_file()}
    systems={};unsupported=[];bindings=[];particle_count=0;partial=[]
    for row in effect_rows:
        filename=nif_table.get(number(row,2),'');source=nifs.get(filename)
        if not source or 'error' in source:
            unsupported.append({'effect':number(row,0),'file':filename,'reason':'missing or invalid NIF'});continue
        if filename not in systems:
            emitters=[]
            for index,e in enumerate(source['emitters']):
                reasons=[]
                source_blend=(e['alphaFlags']>>1)&15;dest_blend=(e['alphaFlags']>>5)&15
                if source_blend!=6 or dest_blend not in (0,7): reasons.append('unsupported source/destination blending')
                for m in e['modifiers']:
                    if m['type'] not in ['gravity','growFade','rotation','color']:reasons.append(m['type'])
                    if m['type']=='gravity' and m['mode']!=0:reasons.append('point gravity')
                basename=Path(e['texture'].replace('\\','/')).name.lower()
                texture=files.get(basename)
                if not texture:
                    texture=next((files.get(Path(basename).stem+ext) for ext in ['.dds','.tga','.bmp','.png'] if files.get(Path(basename).stem+ext)),None)
                if not texture:reasons.append('missing sprite texture')
                if reasons:
                    unsupported.append({'file':filename,'emitter':index,'reason':', '.join(sorted(set(reasons)))});continue
                png='T_fx_'+hashlib.sha1(texture.name.lower().encode()).hexdigest()[:16]+'.png'
                if not (args.output/png).exists():
                    with Image.open(texture) as im:im.convert('RGBA').save(args.output/png)
                life_max=max(.05,min(10,e['life']+max(0,e['lifeRandom'])))
                rate=e['rate'] if e.get('emitFlags',1)&1 else e['capacity']/max(.05,e['life']+e['lifeRandom']*.5)
                recipe=dict(e,rate=rate,texturePng=png,additive=dest_blend==0)
                emitters.append(recipe);particle_count+=1
            if source['meshes']:partial.append({'file':filename,'omittedMeshLayers':source['meshes']})
            if emitters and (not args.limit_systems or len(systems)<args.limit_systems):
                asset='NS_fx_'+hashlib.sha1(filename.encode()).hexdigest()[:16]
                systems[filename]={'asset':asset,'emitters':emitters}
        system=systems.get(filename)
        if not system:continue
        life_max=lambda e:max(.05,min(10,e['life']+max(0,e['lifeRandom'])))
        cost=sum(math.ceil(min(max(1,e['capacity']),e['rate']*life_max(e))) for e in system['emitters'])
        duration=number(row,4,1000)/1000
        rate=max(.1,min(4,number(row,5,100)/100))
        bindings.append({'id':number(row,0),'asset':system['asset'],'socket':row[3],
                         'duration':duration or 1,'playbackRate':rate,'cost':cost,'autoScale':number(row,7),
                         'tail':max(life_max(e) for e in system['emitters'])})
    spells=[]
    for r in rows(args.client/'spells.csv'):
        spells.append({'id':number(r,0),'gather':[number(r,i) for i in [6,7] if number(r,i)],
                       'release':[number(r,i) for i in [8,9] if number(r,i)],
                       'projectile':[number(r,10)] if number(r,10) else [],
                       'hit':[number(r,11)] if number(r,11) else [],
                       'miss':[number(r,16)] if number(r,16) else []})
    (args.output/'manifest.json').write_text(json.dumps({'systems':list(systems.values()),'bindings':bindings,'spells':spells},indent=2))
    report={'sourceNifs':len(nifs),'sourceEmitters':sum(len(n.get('emitters',[])) for n in nifs.values()),
            'systems':len(systems),'convertedEmitters':sum(len(s['emitters']) for s in systems.values()),
            'bindings':len(bindings),'spells':len(spells),'unsupported':unsupported,'partial':partial}
    (args.output/'coverage.json').write_text(json.dumps(report,indent=2))
    print({k:v for k,v in report.items() if k not in ['unsupported','partial']})

if __name__=='__main__':main()
