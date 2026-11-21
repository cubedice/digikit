import { RpcFiles } from '@digi/dk-session/rpcfiles';
import { expect, test } from 'vitest';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, shot, withPausedFxProtocol, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { dspWords, pickSine, REDRAW, word } from '@digi/regression/runtime/fx-live.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';

describeOn('dt2','RELAY track SRC pages',()=>{
  const id='digikit/relay-machine';
  const ctx=withPreparedFxSession('dt2','RELAY track SRC controls',{
    image:{name:'relay-track', modules:['core/relay-track','core/digikit-menu','core/health','core/input'],
      inventory:[id],symbols:['DKRT_BOOT','fx.rows','_relay_status','relay_files'],policy:'settings'},
    usedCard:true,initial:'empty',presets:{empty:{seedProject:true,selection:['modules/m0.dkm']}}
  });
  async function selectRelay() {
    const {fixture,plan,artifacts}=ctx, runtime=fixture.runtime, session=runtime.session, rt=sessionRt(session);
    const boot=await inspectFxBoot(runtime,plan,[id],artifacts,'boot',[0],{afterEpoch:runtime.epoch-1});
    ctx.rememberEpoch(boot.epoch);
    const want=boot.modules[0]!.types[0];
    // Keep the seeded 440 Hz reference on T01 and record it on a fresh T13.
    await rt.ui.tap('TRIG 13',{held:'TRK',until:{ms:200,why:REDRAW}});
    const selected=async()=>((await word(session,ctx.profile.track_type_word+12*0x450))>>>8)&255;
    for(const down of [7,8,6]){await pickSine(runtime,down,12);if(await selected()===want)break;}
    expect(await selected()).toBe(want);
    const tap=(key:string,ms=200)=>rt.ui.tap(key,{until:{ms,why:REDRAW}});
    return {plan,artifacts,runtime,session,rt,tap,want};
  }
  test('selects the track machine, records on LIVE and captures an alternate on TAKE',async()=>{
    const {plan,artifacts,runtime,session,rt,tap}=await selectRelay();
    const status=()=>dspWords(session,planSymbol(plan,'_relay_status'),14);
    await tap('AMP');
    await tap('SRC');
    await rt.ui.tap('TRIG 1',{held:'PUSH A',until:{ms:200,why:REDRAW}});
    // A four-step master on the disposable emulator pattern exercises the same
    // recording boundary as a long master while keeping the sample transfer short.
    const pattern=(await session.client.memPeek(0x4316501c))!;
    await session.client.call('mem.cf.write',{addr:pattern+0x1db74,hex:'0004'});
    await session.client.call('mem.cf.write',{addr:0x43165130,hex:'0004'});
    await tap('PUSH E',35);
    expect((await status())[2]! & 15).toBe(1);
    const writer=await dspWords(session,planSymbol(plan,'_rtm_tracks')+12*64,16);
    expect(writer[14],'recorder source is track 1').toBe(3);
    await session.client.call('pcm.tap',{action:'start'});
    await tap('TRIG 1',100);
    const source=await session.client.call<{peak:number}>('pcm.tap',{action:'report'});
    expect(source.peak,'T01 reference sounds during recording').toBeGreaterThan(0.001);
    await rt.ui.tap('TRIG 13',{held:'TRK',until:{ms:55,why:REDRAW}});
    for(let i=0;i<40 && ((await status())[2]! & 15);++i)
      await rt.ui.wait({ms:200,why:'wait for the complete pattern-length tape'});
    const recorded=await status();
    expect(recorded[2]! & 15,'first pass closes after one pattern: '+JSON.stringify(recorded)).toBe(0);
    expect(recorded[4]! & 255).toBe(12);
    expect(recorded[11]! >>> 8).toBe(4);
    expect(recorded[5],JSON.stringify(recorded)).toBeGreaterThan(0);
    const fileState=()=>word(session,planSymbol(plan,'relay_files'));
    for(let i=0;i<24 && await fileState()!==5;++i) await rt.ui.wait({ms:150,why:REDRAW});
    if (await fileState()!==5) await shot(session,artifacts,'waveform-pending');
    expect(await fileState(),'LIVE waveform finished: '+JSON.stringify(await status())).toBe(5);
    const peakBase=planSymbol(plan,'relay_files')+(7+67)*4;
    const pairs=await Promise.all(Array.from({length:60},(_,i)=>word(session,peakBase+4*i)));
    expect(pairs.some(x=>x!==0),'waveform contains the recorded reference, status '+JSON.stringify(recorded)).toBe(true);
    const changeTrack=(n:number)=>rt.ui.tap('TRIG '+n,{held:'TRK',until:{ms:100,why:REDRAW}});
    const roundTrip=async()=>{await changeTrack(1);await changeTrack(13);};
    await tap('PUSH F',320);
    expect((await status())[7]! & 1,'F stops the recorded loop').toBe(0);
    const parked=(await status())[6];
    await roundTrip();
    expect((await status())[6],'track selection preserves a stopped head').toBe(parked);
    expect((await status())[7]! & 1).toBe(0);
    expect(await fileState(),'completed waveform available immediately').toBe(5);
    const stoppedPixels=async()=>{
      const frame=await session.client.framebufferGet({data:true});
      const bytes=Buffer.from(frame.data_b64!, 'base64');
      // Compare the LIVE body; the stock track meter/header may keep animating.
      return bytes.filter((_,i)=>i>=16*8 && i%8<=5);
    };
    const still=await stoppedPixels();
    for(let i=0;i<3;++i) {
      await rt.ui.wait({ms:90,why:'sample the stationary waveform and status row'});
      expect((await stoppedPixels()).equals(still)).toBe(true);
    }
    const readFrame=(s:number[])=> (s[6]! + (s[2]! >>> 16) + ((s[4]! & 0xff00) << 8)) % s[5]!;
    const turn=(enc:string,n:number)=>rt.ui.turn(enc,n,{until:{ms:120,why:REDRAW}});
    await turn('ENC F',2);
    const moved=await status();
    expect(moved[6]).toBe(parked);
    expect(readFrame(moved)).not.toBe(parked);
    await turn('ENC E',-2);
    const written=await status();
    expect(written[6]).not.toBe(parked);
    expect(readFrame(written)).toBe(readFrame(moved));
    await tap('PUSH F',35);
    await tap('PUSH F',320);
    const homed = await status();
    const slip = (homed[2]! >>> 16) | ((homed[4]! & 0xff00) << 8);
    expect((homed[6]! + slip) % homed[5]!, 'double-click homes only the reader').toBe(0);
    expect(homed[6]).toBe(written[6]);
    expect(homed[7]! & 1).toBe(0);
    await session.client.call('pcm.tap',{action:'start'});
    await tap('PUSH F',320);
    await rt.ui.wait({ms:Math.ceil(recorded[5]!/48)+100,why:'listen to a complete tape lap'});
    const audio=await session.client.call<{peak:number;non_finite:number}>('pcm.tap',{action:'report'});
    expect(audio.non_finite).toBe(0);
    expect(audio.peak,'the recorded reference plays with the source note released').toBeGreaterThan(0.001);
    expect((await status())[7],'F opens a native track voice without a sequencer trig').toBe(1);
    const lapPeak=async()=>{
      await session.client.call('pcm.tap',{action:'start'});
      await rt.ui.wait({ms:Math.ceil(recorded[5]!/48)+40,why:'measure a complete tape lap'});
      return (await session.client.call<{peak:number}>('pcm.tap',{action:'report'})).peak;
    };
    const prior=await lapPeak();
    await roundTrip();
    const after=await lapPeak();
    expect(prior).toBeGreaterThan(0.001);
    expect(after,'playback level after track selection').toBeCloseTo(prior,2);
    expect(after).toBeGreaterThan(prior*0.95);
    await tap('PUSH E');
    expect((await status())[2]! & 15).toBe(2);
    await roundTrip();
    expect((await status())[2]! & 15,'punch survives track selection').toBe(2);
    await tap('PUSH E');
    expect((await status())[2]! & 15).toBe(0);
    await tap('STOP');
    expect((await status())[7]! & 1,'stock STOP also stops manual playback').toBe(0);
    await tap('PLAY');
    expect((await status())[7]! & 1,'stock PLAY launches recorded tape').toBe(1);
    await tap('STOP');
    expect((await status())[7]! & 1).toBe(0);
    await shot(session,artifacts,'live');
    await tap('SRC');
    await tap('PUSH E');
    for(let i=0;i<85 && !((await status())[8]! & 1);++i)
      await rt.ui.wait({ms:200,why:'wait for a full alternate pass'});
    const take=await status();
    expect(take[8]! & 1,JSON.stringify(take)).toBe(1);
    await tap('PUSH F');
    await roundTrip();
    expect((await status())[8]! & 4,'selecting a track preserves the active audition').toBe(4);
    await tap('NO');
    expect((await status())[8]! & 4).toBe(0);
    await shot(session,artifacts,'take');
    await tap('PUSH G');
    await tap('YES');
    const path='/RELAY/T13-0001/FULL';
    for(let n=0;n<90 && await fileState()===1;++n) await rt.ui.wait({ms:350,why:REDRAW});
    const saved=await withPausedFxProtocol(runtime,dk=>new RpcFiles(dk).info(path));
    expect(saved.found,'FULL exists in the stock sample browser').toBe(true);
    expect(saved.size).toBe(64+4*recorded[5]!);
  },240000);
  test('routes the stock MOD picker to the RELAY tape',async()=>{
    const {plan,artifacts,session,rt,tap,want}=await selectRelay();
    const turn=(enc:string,n:number)=>rt.ui.turn(enc,n,{until:{ms:130,why:REDRAW}});
    await tap('AMP');
    await tap('SRC');
    await turn('ENC D',-48);
    const tape=planSymbol(plan,'_rtm_tapes')+12*512;
    const base=(await dspWords(session,tape+5*4,1))[0]!;
    expect(base).toBeGreaterThan(0);
    expect(base).toBeLessThan(0x3f800000);
    // Use the real stock destination picker and native LFO on T13. Mirror
    // 28 exercises the stock sample-slot conversion bypass as well.
    await tap('MOD');
    await turn('ENC D',1);
    for(let i=0;i<5;++i) await tap('DOWN',45);
    await shot(session,artifacts,'mod-feedback-picker');
    await tap('YES',3500);
    const sound=ctx.profile.track_type_word+12*0x450-0xa0;
    const mirror=async(i:number)=>{
      const addr=sound+0x14+2*i, value=await word(session,addr & ~3);
      return addr & 2 ? value & 65535 : value >>> 16;
    };
    expect(await mirror(4),'stock LFO destination').toBe(28<<8);
    for(let n=0;n<16 && await mirror(8)<0x5000;++n) await turn('ENC H',32);
    expect(await mirror(8)).toBeGreaterThanOrEqual(0x5000);
    await tap('TRIG 13');
    const feedback=new Set<number>(), carriers=new Set<number>();
    // Completed native frame at the documented dt2-1.17 stock DSP address.
    const frame=0x2558dc;
    const type=(await dspWords(session,frame+(37+6)*4,1))[0]! & 65535;
    const state=await dspWords(session,tape+17*4,2);
    expect([type,...state],'frame owner, configured type and primed flag').toEqual([want,want,1]);
    for(let n=0;n<12;++n) {
      await rt.ui.wait({ms:80,why:'sample native LFO modulation on the tape'});
      feedback.add((await dspWords(session,tape+21*4,1))[0]!);
      carriers.add((await dspWords(session,frame+(24*12+56)*4,1))[0]! & 65535);
    }
    expect(carriers.size,'native feedback carrier '+JSON.stringify([...carriers])).toBeGreaterThan(1);
    expect(feedback.size,'stock LFO reaches this tape: '+JSON.stringify([...feedback])).toBeGreaterThan(1);
    expect((await dspWords(session,tape+5*4,1))[0],'stored feedback stays at its knob value').toBe(base);
  },150000);
});
