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
"""

from __future__ import annotations

__all__ = ["COMMENTER", "inject"]

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


def inject(html: str) -> str:
    """Return *html* with the commenter appended (idempotent).

    >>> inject("<p>x</p></body>").count('id="cmt-tab"')
    1
    >>> inject(inject("<p>x</p></body>")).count('id="cmt-tab"')
    1
    """
    if 'id="cmt-tab"' in html:
        return html
    if "</body>" in html:
        return html.replace("</body>", COMMENTER + "</body>", 1)
    return html + COMMENTER
