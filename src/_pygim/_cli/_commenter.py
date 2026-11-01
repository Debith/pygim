# -*- coding: utf-8 -*-
"""The docs commenter — a review layer stamped into every served HTML page.

The fragment adds a ✎ button (comment mode: click anywhere to annotate) and
select-to-comment (selecting text pops a ✎ Comment button above the selection).
The server (``oo docs serve``, see ``_docs_serve.py``) stores comments with
their page context in ``__notes__/site-comments.jsonl`` under the served root
and injects this fragment into ANY ``.html`` it serves — every page can be
improved over time, so every page is commentable.

The review loop: leave comments in the browser, then read the JSONL file and act
on them.

The same layer carries ``READER``, for a page made from Markdown: a line under its title
saying when it changed and when it was read (or a "mark as read" link), and what is new:
what changed since it was read or — until it first is — what differs from its last commit.
A box to step between the changes appears only while there are some — first, previous,
next, last (scrolling moves it too: the change nearest the middle of the window is the one
you are on), and ✓ to mark just the change you are on read — and marking the page read
takes it away. Added blocks get a
green background and edge, changed ones amber and a "before" chip that shows what they
said; the tint is layered over a block's own background, so a code block stays a code
block. Nothing it adds moves the page — the tint reaches past a block by its shadow, not
by padding — so comment pins placed by position stay where they were.

And ``DIAGRAMS``: every drawn Mermaid diagram gets a ⛶ button that opens it full screen,
where the wheel zooms toward the pointer, a drag moves it, and Esc closes.
"""

from __future__ import annotations

__all__ = ["COMMENTER", "DIAGRAMS", "READER", "inject"]

COMMENTER = """
<style>
#cmt-tab{position:fixed;right:.8rem;bottom:.8rem;z-index:95;width:38px;height:38px;border-radius:50%;
  background:var(--ink-3,#161f28);border:1px solid var(--line,rgba(231,225,212,.2));color:var(--brass,#c9a24a);
  font-size:1rem;cursor:pointer;box-shadow:0 4px 14px rgba(0,0,0,.4)}
#cmt-tab.on{background:var(--brass,#c9a24a);color:#1a1206}
body.cmt-mode{cursor:crosshair}
.cmt-pin{position:absolute;z-index:70;width:14px;height:14px;border-radius:50%;background:var(--brass,#c9a24a);
  border:2px solid #1a1206;cursor:pointer;box-shadow:0 2px 6px rgba(0,0,0,.5)}
.cmt-pin:hover{transform:scale(1.25)}
#cmt-form{position:absolute;z-index:96;background:#0f1519;border:1px solid var(--brass,#c9a24a);border-radius:4px;
  padding:.6rem;width:min(320px,86vw);box-shadow:0 10px 34px rgba(0,0,0,.6)}
#cmt-form textarea{width:100%;height:74px;background:var(--ink-2,#111820);color:var(--paper,#e7e1d4);
  border:1px solid var(--line,rgba(231,225,212,.2));border-radius:3px;font:inherit;font-size:.82rem;padding:.4rem}
#cmt-form .row{display:flex;gap:.5rem;margin-top:.45rem;align-items:center}
#cmt-form button{background:var(--ink-2,#111820);border:1px solid var(--line,rgba(231,225,212,.2));
  color:var(--paper,#e7e1d4);border-radius:3px;padding:.3rem .7rem;font-size:.75rem;cursor:pointer}
#cmt-form button.save{border-color:var(--brass,#c9a24a);color:var(--brass,#c9a24a)}
#cmt-form .ctx{font-size:.62rem;color:#8b98a3;margin-top:.35rem;font-family:monospace;word-break:break-all}
#cmt-pop{position:absolute;z-index:96;background:#0f1519;border:1px solid var(--line,rgba(231,225,212,.3));
  border-radius:4px;padding:.55rem .7rem;max-width:320px;font-size:.8rem;box-shadow:0 10px 34px rgba(0,0,0,.6)}
#cmt-pop .d{font-size:.62rem;color:#8b98a3;margin-top:.3rem;font-family:monospace}
#cmt-pop .r{display:flex;gap:.4rem;margin-top:.5rem}
#cmt-pop button{background:var(--ink-2,#111820);border:1px solid var(--line,rgba(231,225,212,.2));
  color:var(--paper,#e7e1d4);border-radius:3px;font-size:.7rem;padding:.15rem .55rem;cursor:pointer}
#cmt-pop button.save{border-color:var(--brass,#c9a24a);color:var(--brass,#c9a24a)}
#cmt-pop textarea{width:100%;min-width:240px;height:72px;background:var(--ink-2,#111820);
  color:var(--paper,#e7e1d4);border:1px solid var(--line,rgba(231,225,212,.2));border-radius:3px;
  font:inherit;font-size:.78rem;padding:.35rem}
#cmt-selbtn{position:absolute;z-index:96;background:#0f1519;border:1px solid var(--brass,#c9a24a);
  color:var(--brass,#c9a24a);border-radius:4px;padding:.25rem .6rem;font:inherit;font-size:.75rem;
  cursor:pointer;box-shadow:0 6px 20px rgba(0,0,0,.55);white-space:nowrap}
#cmt-selbtn:hover{background:var(--brass,#c9a24a);color:#1a1206}
</style>
<script>
(function(){ // a "‹ Back" link in every page header (or a floating one when the page has no header.top)
var top=document.querySelector("header.top");
if(top&&history.length>1){var b=document.createElement("a");b.className="btn";b.href="#";b.textContent="\\u2039 Back";
  b.addEventListener("click",function(e){e.preventDefault();history.back();});
  var sp=top.querySelector(".top__spacer");
  if(sp&&sp.nextSibling)top.insertBefore(b,sp.nextSibling);else top.appendChild(b);}
else if(!top&&history.length>1){var f=document.createElement("a");f.href="#";f.textContent="\\u2039 Back";f.id="cmt-back";
  f.style.cssText="position:fixed;top:.6rem;left:.8rem;z-index:95;background:#161f28;border:1px solid rgba(231,225,212,.2);color:#c9a24a;border-radius:4px;padding:.25rem .7rem;font:600 .72rem/1.2 system-ui,sans-serif;letter-spacing:.08em;text-decoration:none";
  f.addEventListener("click",function(e){e.preventDefault();history.back();});document.body.appendChild(f);}
})();
(function(){
if(location.protocol!=="http:"&&location.protocol!=="https:")return;
var tab=document.createElement("button");tab.id="cmt-tab";
tab.title="Comment mode — click anywhere to annotate. Or select text and click the \\u270e Comment button.";
tab.textContent="\\u270e";document.body.appendChild(tab);
var mode=false,form=null,pop=null;
function headingFor(el){var hs=document.querySelectorAll("h1,h2,h3,h4,h5"),best=null;
  for(var i=0;i<hs.length;i++){if(hs[i]===el||hs[i].compareDocumentPosition(el)&Node.DOCUMENT_POSITION_FOLLOWING)best=hs[i];}
  return best?best.textContent.trim().slice(0,120):null;}
function ctxFor(el,pageX,pageY){var anc=el.closest?el.closest("[id]"):null;
  var sel=window.getSelection?String(getSelection()).trim():"";
  return{page:location.pathname,title:document.title,when:new Date().toISOString(),
    anchor:anc?anc.id:null,relX:anc?Math.round(pageX-(anc.getBoundingClientRect().left+scrollX)):null,relY:anc?Math.round(pageY-(anc.getBoundingClientRect().top+scrollY)):null,element:el.tagName.toLowerCase()+(el.className&&el.className.split?"."+el.className.split(" ")[0]:""),
    heading:headingFor(el),excerpt:(el.innerText||el.textContent||"").trim().replace(/\\s+/g," ").slice(0,240),
    selection:sel?sel.slice(0,400):null,docX:Math.round(pageX),docY:Math.round(pageY)};}
function closeForm(){if(form){form.remove();form=null;}}
function closePop(){if(pop){pop.remove();pop=null;}}
var PINS=[];function placeAll(){PINS.forEach(function(f){f();});}
function pin(c){var p=document.createElement("div");p.className="cmt-pin";p.title=c.text;
  function place(){var el=c.anchor?document.getElementById(c.anchor):null;
    if(el&&c.relX!=null&&c.relY!=null){var r=el.getBoundingClientRect();p.style.left=(r.left+scrollX+c.relX-7)+"px";p.style.top=(r.top+scrollY+c.relY-7)+"px";}
    else{p.style.left=(c.docX-7)+"px";p.style.top=(c.docY-7)+"px";}}
  place();PINS.push(place);
  p.addEventListener("click",function(e){e.stopPropagation();closePop();pop=document.createElement("div");
    pop.id="cmt-pop";pop.style.left=(p.offsetLeft+19)+"px";pop.style.top=(p.offsetTop+11)+"px";
    var d=document.createElement("div");d.textContent=c.text;pop.appendChild(d);
    var m=document.createElement("div");m.className="d";
    function meta(){m.textContent=(c.stored||c.when||"")+(c.edited?" · edited":"")+(c.heading?" · "+c.heading:"");}
    meta();pop.appendChild(m);
    var row=document.createElement("div");row.className="r";
    var ed=document.createElement("button");ed.textContent="Edit";
    var del=document.createElement("button");del.textContent="×";del.title="Delete comment";
    row.appendChild(ed);row.appendChild(del);pop.appendChild(row);
    ed.addEventListener("click",function(ev){ev.stopPropagation();
      if(pop.querySelector("textarea"))return;
      var ta=document.createElement("textarea");ta.value=c.text;
      var save=document.createElement("button");save.className="save";save.textContent="Save";
      d.textContent="";d.appendChild(ta);row.insertBefore(save,ed);ed.style.display="none";ta.focus();
      function done(){p.title=c.text;d.textContent=c.text;save.remove();ed.style.display="";meta();}
      save.addEventListener("click",function(e2){e2.stopPropagation();var tv=ta.value.trim();if(!tv)return;
        fetch("/comment-edit",{method:"POST",headers:{"Content-Type":"application/json"},
          body:JSON.stringify({id:c.id,text:tv})}).then(function(r){if(!r.ok)throw 0;c.text=tv;c.edited=1;done();closePop();})
          .catch(function(){alert("Could not save edit — is oo docs serve running?");});});
      ta.addEventListener("keydown",function(e3){if(e3.key==="Escape")done();if(e3.key==="Enter"&&(e3.ctrlKey||e3.metaKey)){e3.preventDefault();save.click();}e3.stopPropagation();});});
    del.addEventListener("click",function(ev){ev.stopPropagation();
      fetch("/comment-delete",{method:"POST",headers:{"Content-Type":"application/json"},
        body:JSON.stringify({id:c.id})}).then(function(r){if(!r.ok)throw 0;closePop();p.remove();})
        .catch(function(){alert("Could not delete");});});
    document.body.appendChild(pop);});
  document.body.appendChild(p);}
function openForm(x,y,ctx){closeForm();form=document.createElement("div");form.id="cmt-form";
  form.style.left=Math.min(x,scrollX+innerWidth-340)+"px";form.style.top=(y+6)+"px";
  var ta=document.createElement("textarea");ta.placeholder="Comment\\u2026 (Ctrl+Enter saves, Esc cancels)";form.appendChild(ta);
  var row=document.createElement("div");row.className="row";
  var save=document.createElement("button");save.className="save";save.textContent="Save";
  var cancel=document.createElement("button");cancel.textContent="Cancel";
  row.appendChild(save);row.appendChild(cancel);form.appendChild(row);
  var cx=document.createElement("div");cx.className="ctx";
  cx.textContent=(ctx.anchor?"#"+ctx.anchor+" \\u00b7 ":"")+(ctx.heading||ctx.element);form.appendChild(cx);
  cancel.addEventListener("click",closeForm);
  save.addEventListener("click",function(){var t=ta.value.trim();if(!t)return;ctx.text=t;
    fetch("/comment",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(ctx)})
      .then(function(r){if(!r.ok)throw 0;return r.json();}).then(function(nc){closeForm();pin(nc);})
      .catch(function(){alert("Could not save \\u2014 is oo docs serve running?");});});
  document.body.appendChild(form);ta.focus();
  ta.addEventListener("keydown",function(e){if(e.key==="Escape")closeForm();if(e.key==="Enter"&&(e.ctrlKey||e.metaKey)){e.preventDefault();save.click();}});}
// the reader's ✎ on a changed block opens this form with that block as the context
window.cmtAt=function(el){var r=el.getBoundingClientRect(),x=r.right+scrollX-8,y=r.bottom+scrollY;
  var ctx=ctxFor(el,x,y);ctx.block=el.getAttribute("data-block");ctx.selection=null;
  ctx.excerpt=(el.innerText||el.textContent||"").trim().replace(/\\s+/g," ").slice(0,240);
  openForm(Math.max(scrollX+8,x-330),y,ctx);};
tab.addEventListener("click",function(){mode=!mode;tab.classList.toggle("on",mode);
  document.body.classList.toggle("cmt-mode",mode);if(!mode){closeForm();}});
document.addEventListener("click",function(e){
  if(pop&&!pop.contains(e.target))closePop();
  if(!mode||form&&form.contains(e.target))return;
  if(e.target===tab||e.target.closest&&(e.target.closest("#cmt-form")||e.target.closest(".cmt-pin")||e.target.closest("#cmt-pop")))return;
  e.preventDefault();e.stopPropagation();
  openForm(e.pageX,e.pageY,ctxFor(e.target,e.pageX,e.pageY));},true);
var selbtn=null;
function closeSelBtn(){if(selbtn){selbtn.remove();selbtn=null;}}
document.addEventListener("mouseup",function(e){
  if(mode)return;
  if(form&&form.contains(e.target)||e.target===tab||pop&&pop.contains(e.target))return;
  if(selbtn&&selbtn.contains(e.target))return;
  setTimeout(function(){ // let the browser finalize the selection first
    closeSelBtn();
    var sel=window.getSelection?getSelection():null,s=sel?String(sel).trim():"";
    if(!s||!sel.rangeCount)return;
    var rect=sel.getRangeAt(0).getBoundingClientRect();
    if(!rect.width&&!rect.height)return;
    var n=sel.anchorNode,el=n?(n.nodeType===1?n:n.parentElement):e.target;
    var px=rect.left+scrollX+rect.width/2,py=rect.top+scrollY;
    selbtn=document.createElement("button");selbtn.id="cmt-selbtn";
    selbtn.textContent="\\u270e Comment";document.body.appendChild(selbtn);
    selbtn.style.left=Math.max(scrollX+4,Math.min(px-selbtn.offsetWidth/2,scrollX+innerWidth-selbtn.offsetWidth-4))+"px";
    selbtn.style.top=Math.max(scrollY+4,py-selbtn.offsetHeight-8)+"px";
    selbtn.addEventListener("mousedown",function(ev){ev.preventDefault();ev.stopPropagation();});
    selbtn.addEventListener("click",function(ev){ev.stopPropagation();
      var ctx=ctxFor(el,px,py);ctx.selection=s.slice(0,400);
      closeSelBtn();openForm(px,py,ctx);});
  },0);
});
document.addEventListener("mousedown",function(e){
  if(selbtn&&!selbtn.contains(e.target))closeSelBtn();});
document.addEventListener("scroll",placeAll,true);window.addEventListener("resize",placeAll);
fetch("/comments?page="+encodeURIComponent(location.pathname)).then(function(r){return r.ok?r.json():[];})
  .then(function(list){list.forEach(function(c){if(c.docX&&c.docY)pin(c);});}).catch(function(){});
})();
</script>
"""


READER = """
<style>
[data-block].rd-added{background-image:linear-gradient(rgba(47,125,79,.16),rgba(47,125,79,.16));
  box-shadow:-9px 0 0 -3px #2f7d4f,0 0 0 6px rgba(47,125,79,.16);border-radius:2px}
[data-block].rd-changed{background-image:linear-gradient(rgba(176,110,20,.16),rgba(176,110,20,.16));
  box-shadow:-9px 0 0 -3px #b06e14,0 0 0 6px rgba(176,110,20,.16);border-radius:2px}
[data-block].rd-removed{background-image:linear-gradient(rgba(179,58,58,.12),rgba(179,58,58,.12));
  box-shadow:-9px 0 0 -3px #b33a3a,0 0 0 6px rgba(179,58,58,.12);border-radius:2px;color:#6b5555;margin:1rem 0}
.rd-removed *{text-decoration:line-through;text-decoration-color:rgba(179,58,58,.7)}
.rd-removed::before{content:"removed";display:block;font:600 10px/1.6 system-ui,sans-serif;letter-spacing:.06em;text-transform:uppercase;color:#b33a3a;text-decoration:none}
@media (forced-colors:active){[data-block].rd-added,[data-block].rd-changed,[data-block].rd-removed{outline:2px solid CanvasText;outline-offset:6px}}
#rd-bar{position:fixed;left:.8rem;bottom:.8rem;z-index:94;max-width:min(440px,calc(100vw - 5rem));
  background:#fffdf7;color:#1d242b;border:1px solid #d9d2c3;border-radius:6px;padding:.5rem .7rem;
  font:13px/1.45 system-ui,sans-serif;box-shadow:0 6px 20px rgba(0,0,0,.12)}
#rd-bar button{font:inherit;font-size:12px;margin:.35rem .4rem 0 0;padding:.15rem .55rem;border:1px solid #cfc7b6;
  border-radius:4px;background:#fff;color:#1d242b;cursor:pointer}
#rd-bar button.mark{border-color:#2f7d4f;color:#2f7d4f}
#rd-bar .gap{display:inline-block;width:.9rem;border-left:1px solid #d9d2c3;height:1.1rem;vertical-align:middle;margin:0 .2rem 0 .5rem}
#rd-bar button.sq.one{border-color:#2f7d4f;color:#2f7d4f;margin-left:.4rem}
#rd-bar button.sq{width:1.7rem;height:1.7rem;padding:0;margin-right:.2rem;text-align:center;line-height:1}
#rd-bar .pos{display:inline-block;min-width:2.6rem;text-align:center;font-variant-numeric:tabular-nums;color:#5f6a72;margin-right:.2rem}
[data-block].rd-here{outline:2px solid #1d242b;outline-offset:8px}
.rd-meta button.mark{font:inherit;color:#2f7d4f;background:none;border:0;padding:0;cursor:pointer;text-decoration:underline;text-underline-offset:2px}
#rd-bar button:focus-visible,.rd-was:focus-visible{outline:2px solid #2f7d4f;outline-offset:2px}
.rd-was{position:absolute;z-index:93;font:600 10px/1 system-ui,sans-serif;letter-spacing:.06em;text-transform:uppercase;
  color:#b06e14;background:#fffdf7;border:1px solid #e2c9a0;border-radius:3px;padding:.25rem .4rem;cursor:pointer}
.rd-cmt{position:absolute;z-index:93;width:1.35rem;height:1.35rem;padding:0;font-size:11px;line-height:1;color:#5f6a72;background:#fffdf7;border:1px solid #d9d2c3;border-radius:3px;cursor:pointer;opacity:.75}
.rd-one{position:absolute;z-index:93;width:1.35rem;height:1.35rem;padding:0;font-size:12px;line-height:1;color:#2f7d4f;background:#fffdf7;border:1px solid #b9d3c1;border-radius:3px;cursor:pointer;opacity:.75}
.rd-one:hover,.rd-one:focus-visible{opacity:1;border-color:#2f7d4f}
.rd-cmt:hover,.rd-cmt:focus-visible{opacity:1;color:#1d242b;border-color:#8a9299}
.rd-pop{position:absolute;z-index:96;max-width:min(64ch,90vw);max-height:50vh;overflow:auto;background:#fffdf7;
  color:#1d242b;border:1px solid #d9d2c3;border-radius:6px;padding:.6rem .8rem;font:14px/1.5 system-ui,sans-serif;
  box-shadow:0 10px 30px rgba(0,0,0,.18)}
.rd-pop .h{font:600 11px/1 system-ui,sans-serif;letter-spacing:.06em;text-transform:uppercase;color:#5f6a72;margin-bottom:.5rem}
</style>
<script>
(function(){ // when this page last changed, and what changed since it was marked read
if(location.protocol!=="http:"&&location.protocol!=="https:")return;
var page=location.pathname,chips=[],pop=null,bar=null,resume=-1,track=null,ticking=false;
function make(tag,cls,text){var e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e;}
function when(iso){if(!iso)return "";var d=new Date(iso);return isNaN(d)?iso:
  d.toLocaleString(undefined,{day:"numeric",month:"short",year:"numeric",hour:"2-digit",minute:"2-digit"});}
function closePop(){if(pop){pop.remove();pop=null;}}
function popover(x,y,title,html){closePop();pop=make("div","rd-pop");pop.appendChild(make("div","h",title));
  var body=make("div");body.innerHTML=html;pop.appendChild(body);document.body.appendChild(pop);
  pop.style.left=Math.max(scrollX+8,Math.min(x,scrollX+innerWidth-pop.offsetWidth-8))+"px";pop.style.top=y+"px";return pop;}
function placeChips(){chips.forEach(function(f){f();});}
function chip(block,before){var c=make("button","rd-was","before");c.title="What this said when the page was marked read";
  function place(){var r=block.getBoundingClientRect();
    c.style.left=Math.min(r.right+scrollX+6,scrollX+innerWidth-c.offsetWidth-4)+"px";c.style.top=(r.top+scrollY)+"px";}
  document.body.appendChild(c);place();chips.push(place);
  c.addEventListener("click",function(e){e.stopPropagation();popover(c.offsetLeft-120,c.offsetTop+c.offsetHeight+4,"Before",before);});}
function markOne(block,k){fetch("/read",{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({page:page,block:block.getAttribute("data-block")})})
  .then(function(r){if(!r.ok)throw 0;return r.json();}).then(function(s){resume=k;show(s);})
  .catch(function(){alert("Could not mark it read \\u2014 is oo docs serve running?");});}
function markOn(block,k){var c=make("button","rd-one","\\u2713");c.title="Mark this change read \\u2014 the others stay new";
  c.setAttribute("aria-label","Mark this change read");
  function place(){var r=block.getBoundingClientRect();
    c.style.left=Math.min(r.right+scrollX-c.offsetWidth+6,scrollX+innerWidth-c.offsetWidth-4)+"px";c.style.top=(r.top+scrollY-6)+"px";}
  document.body.appendChild(c);place();chips.push(place);
  c.addEventListener("click",function(e){e.stopPropagation();markOne(block,k);});}
function commentOn(block){var c=make("button","rd-cmt","\\u270e");c.title="Comment on this change";
  c.setAttribute("aria-label","Comment on this change");
  function place(){var r=block.getBoundingClientRect();
    c.style.left=Math.min(r.right+scrollX-c.offsetWidth+6,scrollX+innerWidth-c.offsetWidth-4)+"px";
    c.style.top=(r.bottom+scrollY-c.offsetHeight+6)+"px";}
  document.body.appendChild(c);place();chips.push(place);
  c.addEventListener("click",function(e){e.stopPropagation();if(window.cmtAt)window.cmtAt(block);});}
function clear(){track=null;document.querySelectorAll(".rd-removed").forEach(function(x){x.remove();});document.querySelectorAll(".rd-added,.rd-changed,.rd-here").forEach(function(b){b.classList.remove("rd-added","rd-changed","rd-here");});
  document.querySelectorAll(".rd-was,.rd-cmt,.rd-one").forEach(function(c){c.remove();});chips=[];closePop();if(bar){bar.remove();bar=null;}}
function markButton(label,count){var b=make("button","mark",label);b.addEventListener("click",function(){
  // marking the whole page read takes in every change at once: ask, since \u2713 marks just one
  if(count>1&&!confirm("Mark all "+count+" changes on this page read?\\n\\n(\u2713 in the box marks only the one you are on.)"))return;
  fetch("/read",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({page:page})})
    .then(function(r){if(!r.ok)throw 0;return r.json();}).then(show)
    .catch(function(){alert("Could not mark it read \\u2014 is oo docs serve running?");});});return b;}
function show(s){clear();
  var meta=document.getElementById("rd-meta");
  if(s.read===false||!meta)return;                  // a page with no Markdown behind it has nothing to mark
  var m=s.modified||{},r=s.read,found=[],fresh=0,gone=r?(r.removed||[]).length:0;
  meta.textContent="Changed "+when(m.changed)+(m.uncommitted?" \\u00b7 not committed":"")+
    (m.commit?" \\u00b7 last commit "+m.commit+", "+when(m.committed):"")+" \\u00b7 ";
  if(r){(r.added||[]).forEach(function(t){var b=document.querySelector('[data-block="'+t+'"]');if(b){b.classList.add("rd-added");found.push(b);}});
    (r.changed||[]).forEach(function(c){var b=document.querySelector('[data-block="'+c.block+'"]');if(!b)return;
      b.classList.add("rd-changed");found.push(b);if(c.before)chip(b,c.before);});
    fresh=found.length;
    // what was deleted is shown where it stood, struck through, and stepped to like any change
    (r.removed||[]).forEach(function(x){var gone_=make("div","rd-removed");gone_.setAttribute("data-block",x.block);
      gone_.innerHTML=x.before;var at_=x.after?document.querySelector('[data-block="'+x.after+'"]'):meta;
      (at_||meta).insertAdjacentElement("afterend",gone_);found.push(gone_);});
    found.sort(function(a,b){return a.compareDocumentPosition(b)&Node.DOCUMENT_POSITION_FOLLOWING?-1:1;});
    found.forEach(function(b,k){commentOn(b);markOn(b,k);});}
  if(!r){meta.appendChild(markButton("mark as read"));return;}
  // until the page is first marked read, what is new is what differs from its last commit
  var since=r.marked?" since you read it, "+when(r.marked):m.commit?" since the last commit ("+m.commit+")":" \u2014 never committed";
  if(!fresh&&!gone){if(r.marked)meta.appendChild(document.createTextNode("read "+when(r.marked)));
    else meta.appendChild(markButton("mark as read"));return;}
  meta.appendChild(document.createTextNode((fresh?fresh+" new or changed":"")+(fresh&&gone?", ":"")+
    (gone?gone+" removed":"")+since));
  // the box is only for moving between the changes, so it is there only while there are some
  bar=make("div");bar.id="rd-bar";
  bar.appendChild(make("div",null,(fresh?fresh+" new or changed":"")+(fresh&&gone?", ":"")+(gone?gone+" removed":"")));
  var row=make("div");
  if(found.length){var at=-1,n=found.length,where=make("span","pos","\\u2013/"+n);
    function setAt(k){if(at===k)return;if(at>=0)found[at].classList.remove("rd-here");at=k;
      if(at>=0)found[at].classList.add("rd-here");where.textContent=(at>=0?at+1:"\\u2013")+"/"+n;}
    function go(k){setAt(Math.max(0,Math.min(n-1,k)));
      var still=matchMedia("(prefers-reduced-motion: reduce)").matches;
      found[at].scrollIntoView({behavior:still?"auto":"smooth",block:"center"});}
    // with no change current, the next one is the first below the middle of the window, the
    // previous one the last above it — never back to the top of the page
    function beside(dir){var mid=innerHeight/2,k;
      if(dir>0){for(k=0;k<n;k++)if(found[k].getBoundingClientRect().top>mid)return k;return n-1;}
      for(k=n-1;k>=0;k--)if(found[k].getBoundingClientRect().bottom<mid)return k;return 0;}
    // scrolling decides too: the change nearest the middle of the window is the one you are on
    track=function(){var mid=innerHeight/2,best=-1,dist=Infinity;
      found.forEach(function(b,k){var r=b.getBoundingClientRect();if(r.bottom<0||r.top>innerHeight)return;
        var d=r.top<=mid&&r.bottom>=mid?0:Math.min(Math.abs(r.top-mid),Math.abs(r.bottom-mid));if(d<dist){dist=d;best=k;}});
      setAt(best);};
    [["\\u21e4","First change",function(){go(0);}],["\\u2039","Previous change",function(){go(at>=0?at-1:beside(-1));}],
     null,["\\u203a","Next change",function(){go(at>=0?at+1:beside(1));}],["\\u21e5","Last change",function(){go(n-1);}],
     ["\\u2713","Mark this change read \\u2014 the others stay new",function(){var k=Math.max(at,0);markOne(found[k],k);}]]
      .forEach(function(b){if(!b){row.appendChild(where);return;}
        var s=make("button","sq"+(b[0]==="\\u2713"?" one":""),b[0]);s.title=b[1];s.setAttribute("aria-label",b[1]);
        s.addEventListener("click",b[2]);row.appendChild(s);});
    if(resume>=0){var r0=resume;resume=-1;go(Math.min(r0,n-1));}}
  row.appendChild(make("span","gap"));
  row.appendChild(markButton("All read",found.length+gone));bar.appendChild(row);document.body.appendChild(bar);}
document.addEventListener("click",function(e){if(pop&&!pop.contains(e.target))closePop();});
document.addEventListener("scroll",placeChips,true);
document.addEventListener("scroll",function(){if(!track||ticking)return;ticking=true;
  requestAnimationFrame(function(){ticking=false;if(track)track();});},{passive:true});window.addEventListener("resize",placeChips);
if(window.ResizeObserver)new ResizeObserver(placeChips).observe(document.body);  // diagrams render late and move blocks
// the server restarts itself when its code changes; a page seeing a new run reloads to get it
var boot=null;setInterval(function(){fetch("/alive").then(function(r){return r.ok?r.json():null;})
  .then(function(a){if(!a)return;if(boot===null)boot=a.boot;else if(a.boot!==boot)location.reload();})
  .catch(function(){});},2000);
fetch("/read-state?page="+encodeURIComponent(page)).then(function(r){return r.ok?r.json():null;})
  .then(function(s){if(s)show(s);}).catch(function(){});
})();
</script>
"""


DIAGRAMS = """
<style>
.dg-full{position:absolute;top:.35rem;right:.35rem;z-index:5;width:1.6rem;height:1.6rem;padding:0;font-size:14px;line-height:1;
  color:#5f6a72;background:#fffdf7;border:1px solid #d9d2c3;border-radius:3px;cursor:pointer;opacity:.7}
.dg-full:hover,.dg-full:focus-visible{opacity:1;color:#1d242b;border-color:#8a9299}
#dg-view{position:fixed;inset:0;z-index:99;background:#fbfbfa;overflow:hidden;cursor:grab;touch-action:none;
  user-select:none;-webkit-user-select:none}
#dg-view.dragging{cursor:grabbing}
#dg-view .stage{position:absolute;left:0;top:0;transform-origin:0 0;pointer-events:none}
#dg-view .bar{position:fixed;top:.7rem;right:.7rem;display:flex;gap:.3rem;z-index:100}
#dg-view .bar button{width:2rem;height:2rem;padding:0;font:15px/1 system-ui,sans-serif;color:#1d242b;background:#fff;
  border:1px solid #cfc7b6;border-radius:4px;cursor:pointer}
#dg-view .bar button:focus-visible{outline:2px solid #2f7d4f;outline-offset:2px}
#dg-view .hint{position:fixed;left:.9rem;bottom:.7rem;font:12px system-ui,sans-serif;color:#5f6a72}
</style>
<script>
(function(){ // every drawn diagram gets a fullscreen view that zooms and pans
function make(tag,cls,text){var e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e;}
function open(svg){var view=make("div");view.id="dg-view";view.setAttribute("role","dialog");view.setAttribute("aria-label","Diagram");
  var stage=make("div","stage"),copy=svg.cloneNode(true),vb=(svg.getAttribute("viewBox")||"").split(/[ ,]+/).map(Number);
  var w=vb.length===4&&vb[2]?vb[2]:(svg.getBoundingClientRect().width||800),h=vb.length===4&&vb[3]?vb[3]:(svg.getBoundingClientRect().height||600);
  copy.removeAttribute("style");copy.setAttribute("width",w);copy.setAttribute("height",h);stage.appendChild(copy);view.appendChild(stage);
  var s=1,x=0,y=0;
  function apply(){stage.style.transform="translate("+x+"px,"+y+"px) scale("+s+")";}
  function fit(){s=Math.min(innerWidth*.92/w,innerHeight*.86/h);x=(innerWidth-w*s)/2;y=(innerHeight-h*s)/2;apply();}
  function zoom(f,px,py){if(px==null){px=innerWidth/2;py=innerHeight/2;}var n=Math.max(.05,Math.min(40,s*f));f=n/s;
    x=px-(px-x)*f;y=py-(py-y)*f;s=n;apply();}
  var bar=make("div","bar");
  [["+","Zoom in",function(){zoom(1.25);}],["\\u2212","Zoom out",function(){zoom(1/1.25);}],
   ["\\u27f2","Fit to screen",fit],["\\u2715","Close (Esc)",close]].forEach(function(b){
    var k=make("button",null,b[0]);k.title=b[1];k.setAttribute("aria-label",b[1]);
    k.addEventListener("click",function(e){e.stopPropagation();b[2]();});bar.appendChild(k);});
  view.appendChild(bar);view.appendChild(make("div","hint","wheel: zoom \\u00b7 drag: move \\u00b7 double-click or 0: fit \\u00b7 Esc: close"));
  view.addEventListener("wheel",function(e){e.preventDefault();zoom(e.deltaY<0?1.15:1/1.15,e.clientX,e.clientY);},{passive:false});
  var drag=null;
  view.addEventListener("pointerdown",function(e){if(e.target.closest(".bar"))return;e.preventDefault();drag={px:e.clientX,py:e.clientY,x:x,y:y};
    view.classList.add("dragging");if(view.setPointerCapture)view.setPointerCapture(e.pointerId);});
  view.addEventListener("pointermove",function(e){if(!drag)return;x=drag.x+e.clientX-drag.px;y=drag.y+e.clientY-drag.py;apply();});
  function end(){drag=null;view.classList.remove("dragging");}
  view.addEventListener("pointerup",end);view.addEventListener("pointercancel",end);
  view.addEventListener("dblclick",function(e){if(!e.target.closest(".bar"))fit();});
  function key(e){if(e.key==="Escape")close();else if(e.key==="+"||e.key==="=")zoom(1.25);else if(e.key==="-")zoom(1/1.25);
    else if(e.key==="0")fit();}
  function close(){document.removeEventListener("keydown",key);view.remove();}
  document.addEventListener("keydown",key);document.body.appendChild(view);fit();view.querySelector(".bar button").focus();}
function arm(){document.querySelectorAll("pre.mermaid").forEach(function(pre){var svg=pre.querySelector("svg");
  if(!svg||pre.querySelector(".dg-full"))return;
  if(getComputedStyle(pre).position==="static")pre.style.position="relative";
  var b=make("button","dg-full","\\u26f6");b.title="Open this diagram full screen \\u2014 zoom and move it there";
  b.setAttribute("aria-label","Open this diagram full screen");
  b.addEventListener("click",function(e){e.stopPropagation();open(pre.querySelector("svg"));});pre.appendChild(b);});}
// Mermaid draws after the page loads, and not all at once: look again until every diagram has its button
var tries=0,timer=setInterval(function(){arm();var left=[].some.call(document.querySelectorAll("pre.mermaid"),function(p){return !p.querySelector(".dg-full");});
  if(!left||++tries>60)clearInterval(timer);},500);
})();
</script>
"""


def inject(html: str) -> str:
    """Return *html* with the review layer — the commenter and the reader — appended (idempotent).

    >>> inject("<p>x</p></body>").count('id="cmt-tab"')
    1
    >>> inject(inject("<p>x</p></body>")).count('id="cmt-tab"')
    1
    """
    if 'id="cmt-tab"' in html:
        return html
    layer = COMMENTER + READER + DIAGRAMS
    if "</body>" in html:
        return html.replace("</body>", layer + "</body>", 1)
    return html + layer
