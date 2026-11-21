import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig, f2w } from '@digi/sharc-rig-kit';

it('records track 16, overdubs, undoes, and keeps an independent take with track 3 selected between commands', async () => {
  const root = path.resolve(import.meta.dirname, '../../../..');
  const headers = path.join(root, 'sdk/dsp/contract');
  const source = 'modules/core/adapters/relay-track/src/dsp';
  const files = ['modules/core/adapters/relay-storage/src/dsp/storage.s',
    'modules/core/adapters/relay-storage/src/dsp/pool.s', 'modules/core/adapters/relay-track/src/dsp/audio.s', source + '/engine.s'];
  const base = 0x20110000, command = base + 512, status = command + 256;
  const rig = await RawRig.open({objects: files.map((f,i) => ({
    text: readFileSync(path.join(root,f),'utf8').replaceAll('\r\n','\n'),
    includes: [headers,path.join(root,source)], base: 0xb90000+i*0x2000
  })), externs: {_relay_external:base, _rtm_command:command, _relay_status:status, _relay_request:status+256,
    _relay_response:status+512, _relay_timing:status+768, _pattern_position_info:status+1024, _rtm_bus:base+4096, _rtm_mod_frame:base+12288},
  returnSw:0xb9f100});
  let seq=0;
  async function block(op:number, track:number, arg=0, source=0) {
    [0,++seq,op,track,source,5,arg,0].forEach((v,i)=>rig.poke(command/4+i,v));
    const r=await rig.call('_rtm_block',{M6:1,M5:0},{reads:[[status/4,14]]});
    expect(r.halt).toBe('breakpoint');
    const row=r.reads[0]!;
    expect(row[1]).toBe(seq);
    expect(row[13]! & 255).toBe(0);
    expect((row[13]! >>> 16) & 127).toBeGreaterThan((row[13]! >>> 8) & 127);
    return row;
  }
  try {
    for(let i=0;i<480;++i) rig.poke(base/4+i,0);
    for(let i=0;i<64;++i)rig.poke(base/4+i,f2w(i<32?0.25:-0.5));
    await block(1,15);
    await block(0,2);
    expect((await block(2,15))[5]).toBe(64);
    // Pin and scan the real recorded stereo data, including its louder right channel.
    async function transfer(id:number, op:number, arg:number, length:number) {
      [0x524c5831,id,op,arg,length].forEach((v,i)=>rig.poke((status+256)/4+i,v));
      const r=await rig.call('_rtm_block',{M6:1,M5:0},{reads:[[(status+512)/4,14]]});
      expect(r.halt).toBe('breakpoint');
      expect(r.reads[0]!.slice(0,2)).toEqual([0x524c5832,id]);
      const reply=r.reads[0]!;
      const checksum=reply.slice(2,13).reduce((acc,v)=>(Math.imul(acc,31)+v)>>>0,id);
      expect(reply[13]).toBe(checksum);
      return reply.slice(0,4);
    }
    expect((await transfer(1,1,15,0))[2]).toBe(64);
    expect((await transfer(2,9,0,64)).slice(2)).toEqual([64,16384]);
    expect((await transfer(3,2,0,1)).slice(2)).toEqual([1,0x2000c000]);
    // The peak reader works in bounded spans across audio blocks and only publishes a full batch.
    const request=(status+256)/4, response=(status+512)/4;
    [0x524c5831,100,10,0,3,1,32,64].forEach((v,i)=>rig.poke(request+i,v));
    for(let i=0;i<3;++i) {
      const r=await rig.call('_rtm_block',{M6:1,M5:0},{reads:[[response,6]]});
      expect(r.halt).toBe('breakpoint');
      if(i<2) expect(r.reads[0]![1]).toBe(3);
      else expect(r.reads[0]!.slice(1)).toEqual([100,3,16384,16384,16384]);
    }
    rig.poke(request+5,65);
    expect((await transfer(101,10,0,1))[2]).toBe(0xffffffff);
    expect((await transfer(4,3,0,0))[2]).toBe(0);
    await block(1,15);
    for(let i=0;i<4;++i)await block(0,15);
    await block(2,15);
    for(let i=0;i<2;++i)await block(0,15);
    expect(((await block(4,15))[10]! & 1)).toBe(1);
    await block(5,15);
    for(let i=0;i<4;++i)await block(0,15);
    expect(((await block(6,15,1))[8]! & 3)).toBe(1);
    expect((await block(7,15))[11]).toBe(0);
    expect((await block(0,2))[5]).toBe(0);
    // Physical report: track 13 records track 12. Give its bus a different
    // signal from EXT so the exported samples prove the routing as well.
    const track12=(base+4096+11*256)/4;
    for(let i=0;i<64;++i) rig.poke(track12+i,f2w(i<32?-0.125:0.75));
    await block(1,12,0,14);
    await block(2,12,0,14);
    expect((await transfer(5,1,12,0))[2]).toBe(32);
    expect((await transfer(6,9,0,32)).slice(2)).toEqual([32,24576]);
    expect((await transfer(7,2,0,1)).slice(2)).toEqual([1,0xf0006000]);
    expect((await transfer(8,3,0,0))[2]).toBe(0);
  } finally {await rig.close();}
},90000);
