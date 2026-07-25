#!/usr/bin/env node
import fs from "node:fs";
import vm from "node:vm";

const browserPath=process.argv[2]||"sample-patch-browser.html";
const html=fs.readFileSync(browserPath,"utf8");
const match=html.match(/<script id="patcher-script">([\s\S]*?)<\/script>/);
if(!match) throw new Error("sample patcher script was not found");

const context={globalThis:null,Int8Array,Uint8Array,Float32Array,ArrayBuffer,DataView,Math,Number,Error,Boolean,String};
context.globalThis=context;
vm.runInNewContext(match[1],context,{filename:browserPath});
const core=context.ALynxSamplePatcherCore;
if(!core) throw new Error("sample patcher core was not exported");

const assert=(condition,message)=>{if(!condition)throw new Error(message)};
const equal=(actual,expected,message)=>{
  assert(actual.length===expected.length,`${message}: length ${actual.length} != ${expected.length}`);
  for(let i=0;i<actual.length;i++)assert(actual[i]===expected[i],`${message}: byte ${i} is ${actual[i]}, expected ${expected[i]}`);
};
const pcm16Wav=(rate,samples)=>{
  const bytes=new Uint8Array(44+samples.length*2),view=new DataView(bytes.buffer);
  const text=(offset,value)=>[...value].forEach((char,index)=>bytes[offset+index]=char.charCodeAt(0));
  text(0,"RIFF"); view.setUint32(4,bytes.length-8,true); text(8,"WAVE"); text(12,"fmt ");
  view.setUint32(16,16,true); view.setUint16(20,1,true); view.setUint16(22,1,true);
  view.setUint32(24,rate,true); view.setUint32(28,rate*2,true);
  view.setUint16(32,2,true); view.setUint16(34,16,true); text(36,"data");
  view.setUint32(40,samples.length*2,true);
  samples.forEach((sample,index)=>view.setInt16(44+index*2,Math.max(-32768,Math.min(32767,Math.round(sample*32767))),true));
  return bytes;
};

// Direct RIFF decoding must retain the file-declared rate and the
// band-limited converter must preserve duration and audible pitch.
const sourceRate=48000,sourceTone=Float32Array.from({length:sourceRate},(_,i)=>Math.sin(2*Math.PI*440*i/sourceRate));
const decodedTone=core.decodeWav(pcm16Wav(sourceRate,sourceTone).buffer);
assert(decodedTone.sourceRate===sourceRate&&decodedTone.channels[0].length===sourceRate,
  "direct WAV decoding lost the declared sample rate or frame count");
const convertedTone=core.convertChannelsForSlicing(decodedTone.channels,decodedTone.sourceRate);
assert(Math.abs(convertedTone.outputDuration-decodedTone.sourceDuration)<=1/core.PCM_RATE,
  "slice conversion did not preserve source duration");
let crossings=0;
for(let i=1;i<convertedTone.pcm.length;i++)if(convertedTone.pcm[i-1]<=0&&convertedTone.pcm[i]>0)crossings++;
const convertedHz=crossings/convertedTone.outputDuration;
assert(convertedHz>435&&convertedHz<445,`440 Hz slice source became ${convertedHz.toFixed(2)} Hz`);
const aliased=core.convertChannelsForSlicing([
  Float32Array.from({length:sourceRate},(_,i)=>Math.sin(2*Math.PI*10000*i/sourceRate))
],sourceRate).pcm;
const aliasRms=Math.sqrt(aliased.slice(64,-64).reduce((sum,value)=>sum+value*value,0)/(aliased.length-128));
assert(aliasRms<.03,`out-of-band source aliased into the Lynx band at RMS ${aliasRms.toFixed(4)}`);

// The slicer conversion retains source amplitude rather than peak-normalizing
// every prospective pad. Channel mixing also happens before resampling.
const mixed=core.convertChannelsForSlicing([
  new Float32Array([.25,.5,-.5,-.25]),
  new Float32Array([.25,0,-.5,.25])
],core.PCM_RATE);
assert(mixed.pcm.length===4,"slice source changed length at its native rate");
equal(core.processFloatSample(mixed.pcm),new Int8Array([32,32,-64,0]),
  "slice source was normalized or channel mixing changed");
const ordinary=core.convertChannels([new Float32Array([.25,.5,-.5,-.25])],core.PCM_RATE);
assert(ordinary.pcm.length===4&&Math.max(...ordinary.pcm)===120,
  "ordinary normalized one-shot conversion regressed while adding the slicer");

// With fade disabled, concatenating equal slices must reproduce one shared
// quantization pass exactly — including the two samples at each boundary.
const source=Float32Array.from({length:16},(_,i)=>(i-8)/16);
const whole=core.processFloatSample(source);
const slices=core.sliceSample(source,0,source.length,4,0,false,0);
assert(slices.length===4&&slices.every(slice=>slice.length===4),
  "equal four-way slicing produced unequal pads");
equal(Int8Array.from(slices.flatMap(slice=>Array.from(slice))),whole,
  "fade-off slicing broke waveform continuity");

// Fade is deliberately optional. A three-sample fade preserves its first
// sample, reaches half level, and makes the final DAC byte zero.
equal(core.fadeOutSample(new Int8Array([100,100,100,100]),3),
  new Int8Array([100,100,50,0]),"micro fade-out shape changed");

// Shared gain precedes tanh, matching the rest of the patcher's processing
// model without introducing normalization.
const driven=core.sliceSample(new Float32Array([.5]),0,1,1,6.020599913,false,0)[0];
assert(driven[0]===127,"shared slicer gain did not hard-clip after +6.02 dB");
const softened=core.sliceSample(new Float32Array([.5]),0,1,1,6.020599913,true,0)[0];
assert(softened[0]===97,"shared slicer gain was not driven into tanh");

try {
  core.sliceSample(new Float32Array(core.SLOT_CAP+1),0,core.SLOT_CAP+1,1);
  throw new Error("oversize slice was accepted");
} catch(problem) {
  assert(String(problem.message).includes("shorten the region"),"oversize slice reported the wrong error");
}
try {
  core.sliceSample(new Float32Array(9),0,9,9);
  throw new Error("nine slices were accepted");
} catch(problem) {
  assert(String(problem.message).includes("one and eight"),"slice-count limit reported the wrong error");
}

for(const contract of [
  "Slice long sample…","Normalization off.","data-slice-start","data-slice-end",
  "data-slice-pad","data-slice-fade","data-slice-gain","data-slice-drive",
  "sliceSample(state.slicer.pcm","source levels were preserved without normalization"
]) assert(html.includes(contract),`slice-to-kit UI contract missing: ${contract}`);

// Smoke the real single-file event path with a tiny DOM and Web Audio stand-in:
// load a valid ROM, open the slicer, decode a long source, select four slices,
// then apply them. This catches rendering/event wiring regressions that the
// pure audio core cannot see.
const listeners={};
const app={
  innerHTML:"",
  addEventListener(type,listener){listeners[type]=listener},
  querySelector(){return null}
};
const documentStub={
  getElementById(){return app},
  createElement(){return {click(){},remove(){}}},
  body:{appendChild(){}}
};
class FakeAudioContext {
  constructor(){this.state="running";this.sampleRate=48000;this.destination={}}
  async resume(){}
  async decodeAudioData(){
    const channel=Float32Array.from({length:80},(_,i)=>Math.sin(i/5)*.4);
    return {numberOfChannels:1,sampleRate:core.PCM_RATE,getChannelData(){return channel}};
  }
  createBuffer(_channels,length){const data=new Float32Array(length);return {getChannelData(){return data}}}
  createBufferSource(){return {connect(){},start(){},stop(){},onended:null}}
}
const uiContext={
  globalThis:null,document:documentStub,window:{AudioContext:FakeAudioContext},
  Int8Array,Uint8Array,Float32Array,ArrayBuffer,DataView,Math,Number,Error,Boolean,String,
  Blob:globalThis.Blob,URL:globalThis.URL,setTimeout
};
uiContext.globalThis=uiContext;
vm.runInNewContext(match[1],uiContext,{filename:browserPath});

const kit=Array.from({length:8},()=>new Int8Array([0]));
const expandedKits=core.expandKits([kit]);
assert(expandedKits.length===8&&expandedKits.slice(1).every(row=>
  row.length===8&&row.every(sample=>sample.length===1&&sample[0]===0)),
  "a short source bank was not expanded to eight silent, fillable kits");
const expandedBank=core.buildPool(expandedKits);
assert(expandedBank[2]===8,
  "an expanded browser bank did not export all eight kits");
const romBytes=new Uint8Array(core.ROM_BYTES);
romBytes.set([76,89,78,88],0);
romBytes.set(core.buildPool([kit]),core.POOL_OFFSET);
const fileTarget=(id,file)=>({
  id,type:"file",value:"",files:[file],matches(){return false}
});
listeners.change({target:fileTarget("romInput",{
  name:"test.lnx",async arrayBuffer(){return romBytes.buffer.slice(0)}
})});
await new Promise(resolve=>setImmediate(resolve));
assert(app.innerHTML.includes("Slice long sample…"),"loaded workspace did not expose Slice to kit");
assert((app.innerHTML.match(/data-kit="/g)||[]).length===8,
  "a one-kit ROM did not expose all eight kit tabs");
for(let kitIndex=0;kitIndex<8;kitIndex++)
  assert(app.innerHTML.includes(`data-kit="${kitIndex}"><b>0${kitIndex}</b>`),
    `kit ${kitIndex} was not displayed with its zero-based software index`);
assert((app.innerHTML.match(/<span>Empty<\/span>/g)||[]).length===7,
  "undeclared ROM kits were not labelled empty");

const clickAction=(action,extra={})=>listeners.click({target:{closest(selector){
  if(selector==="[data-kit]")return null;
  if(selector==="[data-action]")return {dataset:{action,...extra}};
  return null;
}}});
const clickKit=kitIndex=>listeners.click({target:{closest(selector){
  if(selector==="[data-kit]")return {dataset:{kit:String(kitIndex)}};
  return null;
}}});
clickKit(7);
assert((app.innerHTML.match(/class="sample-card[^"]*empty/g)||[]).length===8,
  "opening undeclared kit 7 did not show eight empty pads");
listeners.change({target:{
  id:"",type:"file",value:"",dataset:{sampleInput:"56"},
  files:[{name:"new-kit-7.wav",async arrayBuffer(){
    return pcm16Wav(core.PCM_RATE,new Float32Array([0,.5,0,-.5])).buffer
  }}],
  matches(selector){return selector==="[data-sample-input]"}
}});
await new Promise(resolve=>setImmediate(resolve));
assert(app.innerHTML.includes("new-kit-7.wav")&&
  (app.innerHTML.match(/class="sample-card[^"]*empty/g)||[]).length===7,
  "an empty kit pad could not be populated with a WAV");

clickAction("toggle-slicer");
assert(app.innerHTML.includes("Drop one longer WAV here"),"empty slicer did not render its drop target");
listeners.change({target:fileTarget("sliceInput",{
  name:"long-loop.wav",async arrayBuffer(){
    return pcm16Wav(core.PCM_RATE,Float32Array.from({length:80},(_,i)=>Math.sin(i/5)*.4)).buffer
  }
})});
await new Promise(resolve=>setImmediate(resolve));
assert(app.innerHTML.includes("Normalization off."),"decoded slicer did not show its continuity contract");
assert((app.innerHTML.match(/class="slice-map-card"/g)||[]).length===8,
  "decoded slicer did not default to eight pad mappings");
assert((app.innerHTML.match(/data-slice-hit=/g)||[]).length===8,
  "decoded slicer did not make all eight waveform slices clickable");
clickAction("audition-slice",{slice:"2"});
await new Promise(resolve=>setImmediate(resolve));
assert(app.innerHTML.includes('class="slice-hit playing"')&&app.innerHTML.includes('data-slice-hit="2"'),
  "clicking a waveform slice did not start and highlight its preview");
clickAction("audition-slice",{slice:"2"});
await new Promise(resolve=>setImmediate(resolve));
assert(!app.innerHTML.includes('class="slice-hit playing"'),
  "clicking the playing waveform slice did not stop its preview");

const countTarget={
  id:"",type:"select-one",value:"4",files:null,checked:false,dataset:{},
  matches(selector){return selector==="[data-slice-count]"}
};
listeners.change({target:countTarget});
assert((app.innerHTML.match(/class="slice-map-card"/g)||[]).length===4,
  "slice-count control did not rebuild four mappings");
clickAction("apply-slices");
assert(app.innerHTML.includes("long-loop.wav · Slice 4"),
  "applying slices did not replace the mapped kit pads");

console.log("sample patch browser: PASS — zero-based KIT 0–7 with fillable empty kits, direct WAV-rate decoding, band-limited pitch/duration conversion, clickable slice preview/stop, unnormalized slicing, pad mapping, shared gain/tanh, optional fade, and slot limits");
