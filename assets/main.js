const copyButton=document.getElementById("copyBuild");
copyButton?.addEventListener("click",async()=>{const code=document.querySelector(".code-panel pre code").innerText;try{await navigator.clipboard.writeText(code);copyButton.textContent="COPIED";setTimeout(()=>copyButton.textContent="COPY",1600)}catch(e){copyButton.textContent="SELECT TO COPY"}});
document.getElementById("year").textContent=new Date().getFullYear();
const toggle=document.getElementById("menuToggle"),nav=document.getElementById("nav");
toggle?.addEventListener("click",()=>{const open=nav.classList.toggle("open");toggle.setAttribute("aria-expanded",String(open));});
nav?.querySelectorAll("a").forEach(a=>a.addEventListener("click",()=>{nav.classList.remove("open");toggle.setAttribute("aria-expanded","false")}));
