import struct, sys
def parse(data):
    if data[:4]!=b'STRM': return None
    off=4
    version,=struct.unpack_from('<I',data,off); off+=4
    fps,=struct.unpack_from('<I',data,off); off+=4
    maxkey,=struct.unpack_from('<I',data,off); off+=4
    layernum,=struct.unpack_from('<I',data,off); off+=4
    off+=16  # reserved
    layers=[]
    for li in range(layernum):
        texcnt,=struct.unpack_from('<i',data,off); off+=4
        texs=[]
        for t in range(texcnt):
            nm=data[off:off+128]; off+=128
            nm=nm.split(b'\0')[0]
            try: texs.append(nm.decode('cp949','replace'))
            except: texs.append(repr(nm))
        framecnt,=struct.unpack_from('<i',data,off); off+=4
        frames=[]
        for f in range(framecnt):
            # keyframe: frame(i), type(i), pos(2f), uv(8f), xy(8f), aniframe(f), anitype(i), delay(f), angle(f), color(4f), srcalpha(i), dstalpha(i), mtpreset(i) = 31 dwords=124 bytes
            kf=struct.unpack_from('<ii2f8f8ffiff4fiii',data,off); off+=124  # 31dw: ...,aniframe,anitype,delay,angle,color[4],src(28),dst(29),mt(30)
            frames.append(kf)
        layers.append((texs,frames))
    return dict(version=version,fps=fps,maxkey=maxkey,layernum=layernum,layers=layers)
if __name__=='__main__':
    import zipfile,os,glob
    z=zipfile.ZipFile('/root/uaro_content/texture_x4.zip')
    names=[n for n in z.namelist() if n.lower().endswith('.str')]
    rows=[]; alltex=set(); bad=0
    for n in names:
        try:
            d=parse(z.read(n))
            if not d: bad+=1; continue
        except Exception as e:
            bad+=1; continue
        base=os.path.basename(n)
        texs=set(); blends=set(); totframes=0
        for texs_l,frames in d['layers']:
            for t in texs_l: texs.add(t); alltex.add(t)
            totframes+=len(frames)
            for kf in frames: blends.add(f"{kf[28]}/{kf[29]}")
        rows.append((base, d['version'], d['fps'], d['maxkey'], d['layernum'], totframes,
                     ';'.join(sorted(t for t in texs if t)) if len(texs)<=5 else str(len(texs))+'tex',
                     ';'.join(sorted(blends))))
    OUT='/root/BornRok/docs/exe-analysis/data'
    with open(OUT+'/str-inventory.tsv','w',encoding='utf-8') as o:
        o.write("str\tversion\tfps\tmaxKey\tlayers\tframes\ttextures\tblend(src/dst)\n")
        for r in sorted(rows): o.write("\t".join(str(x) for x in r)+"\n")
    print("parsed:",len(rows),"bad:",bad,"distinct .str textures:",len(alltex))
    vers=set(r[1] for r in rows); print("versions:",sorted(vers))
