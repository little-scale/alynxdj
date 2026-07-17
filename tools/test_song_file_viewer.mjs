#!/usr/bin/env node
/* Codec regression for the standalone song-file-viewer.html tool. */
import fs from "node:fs";
import vm from "node:vm";

const htmlPath = new URL("../song-file-viewer.html", import.meta.url);
const html = fs.readFileSync(htmlPath, "utf8");
const script = html.match(/<script>\s*([\s\S]*?)<\/script>/)?.[1];
if (!script) throw new Error("viewer script not found");

function element() {
  const listeners=new Map();
  const drawing={clearRect(){},fillRect(){},beginPath(){},moveTo(){},lineTo(){},stroke(){},fillStyle:"",strokeStyle:"",lineWidth:1};
  return {
    hidden:false, disabled:false, value:"", textContent:"", innerHTML:"",
    dataset:{}, style:{}, files:[],
    classList:{ add(){}, remove(){}, toggle(){} },
    addEventListener(type,listener){if(!listeners.has(type))listeners.set(type,[]);listeners.get(type).push(listener)},
    dispatch(type,event={target:this}){for(const listener of listeners.get(type)||[])listener(event)},
    setAttribute(){}, appendChild(){}, remove(){}, click(){this.dispatch("click")}, getContext(){return drawing},
  };
}

const elements = new Map();
const document = {
  body:element(),
  getElementById(id) {
    if (!elements.has(id)) elements.set(id,element());
    return elements.get(id);
  },
  querySelectorAll(){ return [] },
  createElement(){ return element() },
};
const window = { confirm(){ return true } };
const context = {
  document, window, console, Uint8Array, ArrayBuffer, Blob, URL,
  Intl, Map, Set, Error, Math, Number, String, Object, RegExp,
  setTimeout(){ return 0 }, clearTimeout(){},
};
vm.runInNewContext(script,context,{filename:"song-file-viewer.html"});
const api = window.ALXSongFile;
if (!api) throw new Error("viewer test API not exposed");

function assert(condition,message) {
  if (!condition) throw new Error(message);
}
function equalBytes(a,b) {
  return a.length === b.length && a.every((value,index)=>value===b[index]);
}
function imageFor(payload,version) {
  const out=new Uint8Array(2048).fill(255);
  out.set([65,76,68,74],0);
  out[4]=payload.length&255; out[5]=payload.length>>8;
  out[6]=payload.reduce((sum,value)=>(sum+value)&255,0);
  out[7]=version; out.set(payload,8);
  return out;
}

elements.get("new-file").dispatch("click");
assert(elements.get("load-view").hidden===true,"blank-file action did not open the editor");
for (const tab of ["overview","song","chains","phrases","instruments","tables","grooves","waves","raw"]) {
  elements.get("tabs").dispatch("click",{target:{closest(){return {dataset:{tab}}}}});
  assert(elements.get("content").innerHTML.length>100,`${tab} view did not render`);
}
const songEdit=element();
songEdit.value="1A"; songEdit.dataset={byte:"0",mode:"hex",max:"31",ff:"1"};
elements.get("content").dispatch("change",{target:songEdit});
elements.get("tabs").dispatch("click",{target:{closest(){return {dataset:{tab:"raw"}}}}});
assert(elements.get("content").innerHTML.includes("0000  1A"),"structured edit did not reach the raw song block");

const fixturePath = process.argv[2];
if (fixturePath) {
  const fixture = new Uint8Array(fs.readFileSync(fixturePath));
  const parsed = api.parseSram(fixture,"hardware.eeprom");
  assert(parsed.sourceVersion===6,"fixture version is not v6");
  assert(parsed.decodedLength===7680,"fixture did not decode to 7,680 bytes");
  const rebuilt = api.serializeSram(parsed.data,fixture,parsed.config);
  const reparsed = api.parseSram(rebuilt.bytes,"rebuilt.eeprom");
  assert(equalBytes(reparsed.data,parsed.data),"fixture data changed on export/import");
  assert(rebuilt.packed.length===parsed.sourcePacked,"canonical fixture packed length changed");

  const edited=Uint8Array.from(parsed.data);
  edited[api.analyzeConstants.OFF.song]=0x1A;
  edited[api.analyzeConstants.OFF.chains+0x1A*32]=0x05;
  edited[api.analyzeConstants.OFF.phrases+0x05*64]=37;
  edited[api.analyzeConstants.OFF.phrases+0x05*64+1]=9;
  edited[api.analyzeConstants.OFF.instruments+9*16+13]=0x26;
  edited[api.analyzeConstants.OFF.waves+31]=128;
  const editedConfig={...parsed.config,present:true,palette:6,sync:1};
  const editedBuild=api.serializeSram(edited,fixture,editedConfig);
  const editedParse=api.parseSram(editedBuild.bytes,"edited.eeprom");
  assert(equalBytes(editedParse.data,edited),"edited song structures changed on export/import");
  assert(editedParse.config.palette===6&&editedParse.config.sync===1,"edited machine config did not round-trip");

  const corrupt=Uint8Array.from(fixture); corrupt[8]^=1;
  let rejected=false;
  try { api.parseSram(corrupt,"corrupt.eeprom") } catch { rejected=true }
  assert(rejected,"checksum corruption was accepted");
}

const legacy3=api.makeBaseSong();
legacy3[api.analyzeConstants.OFF.instruments+12]=17;
legacy3[api.analyzeConstants.OFF.instruments+13]=34;
legacy3[api.analyzeConstants.OFF.instruments+14]=51;
legacy3[api.analyzeConstants.OFF.instruments+15]=12;
const parsed3=api.parseSram(imageFor(api.packRle(legacy3),3),"v3.eeprom");
assert(parsed3.data[api.analyzeConstants.OFF.instruments+12]===0,"v3 sweep was not cleared");
assert(parsed3.data[api.analyzeConstants.OFF.instruments+13]===0,"v3 vibrato was not cleared");
assert(parsed3.data[api.analyzeConstants.OFF.instruments+14]===0,"v3 tremolo was not cleared");
assert(parsed3.data[api.analyzeConstants.OFF.instruments+15]===0,"v3 transpose was not cleared");

const legacy1=api.makeBaseSong();
const io=api.analyzeConstants.OFF.instruments;
legacy1[io+2]=64; legacy1[io+3]=20; legacy1[io+4]=43;
const legacy1Payload=api.packRle(legacy1.slice(0,api.analyzeConstants.OFF.waves));
const parsed1=api.parseSram(imageFor(legacy1Payload,1),"v1.eeprom");
assert(parsed1.data[io+2]===0x23,"v1 attack/decay rates migrated incorrectly");
assert(parsed1.data[io+3]===15,"v1 hold did not clamp to 15");
assert(parsed1.data[io+4]===0,"v1 obsolete decay byte was not cleared");
assert(parsed1.data[api.analyzeConstants.OFF.waves]===136,"legacy factory wave was not restored");

const noisy=Uint8Array.from({length:7680},(_,index)=>(index*73+(index>>3))&255);
let tooBig=false;
try { api.serializeSram(noisy,null,{present:false,palette:0,prelisten:0,repeat:12,sync:0,reserved:65535}) } catch { tooBig=true }
assert(tooBig,"over-capacity song export was accepted");

const fatDirectory=new Uint8Array(128);
fatDirectory.fill(32,0,11); fatDirectory[0]=46; fatDirectory[11]=0x12;
fatDirectory.fill(32,32,43); fatDirectory[32]=46; fatDirectory[33]=46; fatDirectory[43]=0x12;
let fatMessage="";
try { api.parseSram(fatDirectory,"broken.sav") } catch(error) { fatMessage=error.message }
assert(fatMessage.includes("FAT directory entries"),"SD-card directory data was not diagnosed precisely");

if (process.argv[3]) {
  let suppliedMessage="";
  try { api.parseSram(new Uint8Array(fs.readFileSync(process.argv[3])),"sd-card.sav") } catch(error) { suppliedMessage=error.message }
  assert(suppliedMessage.includes("FAT directory entries"),"supplied SD-card file did not match the expected directory-data diagnosis");
}

console.log(`song file viewer: PASS${fixturePath?" — "+fixturePath:""}`);
