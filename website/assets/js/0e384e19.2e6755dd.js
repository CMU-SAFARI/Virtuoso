"use strict";(self.webpackChunkvirtuoso=self.webpackChunkvirtuoso||[]).push([[976],{7879:(e,t,n)=>{n.r(t),n.d(t,{assets:()=>l,contentTitle:()=>a,default:()=>h,frontMatter:()=>o,metadata:()=>r,toc:()=>d});const r=JSON.parse('{"id":"intro","title":"Getting Started","description":"Virtuoso is a new simulation framework designed to enable fast and accurate prototyping and evaluation of virtual memory (VM) schemes.  It employs a lightweight userspace kernel, MimicOS, which imitates the desired OS kernel code, allowing researchers to simulate only the relevant OS routines and easily develop new OS modules.  Virtuoso\'s imitation-based OS simulation methodology facilitates the evaluation of hardware/OS co-designs by accurately modeling the interplay between OS routines and hardware components.","source":"@site/docs/intro.md","sourceDirName":".","slug":"/intro","permalink":"/Virtuoso/docs/intro","draft":false,"unlisted":false,"editUrl":"https://github.com/facebook/docusaurus/tree/main/packages/create-docusaurus/templates/shared/docs/intro.md","tags":[],"version":"current","sidebarPosition":1,"frontMatter":{"sidebar_position":1},"sidebar":"tutorialSidebar","next":{"title":"Release Notes","permalink":"/Virtuoso/docs/release_notes"}}');var i=n(4848),s=n(8453);const o={sidebar_position:1},a="Getting Started",l={},d=[{value:"Hardware Requirements",id:"hardware-requirements",level:2},{value:"Software Requirements",id:"software-requirements",level:2},{value:"Getting Started",id:"getting-started-1",level:2}];function c(e){const t={code:"code",h1:"h1",h2:"h2",header:"header",li:"li",ol:"ol",p:"p",pre:"pre",strong:"strong",ul:"ul",...(0,s.R)(),...e.components};return(0,i.jsxs)(i.Fragment,{children:[(0,i.jsx)(t.header,{children:(0,i.jsx)(t.h1,{id:"getting-started",children:"Getting Started"})}),"\n",(0,i.jsx)(t.p,{children:"Virtuoso is a new simulation framework designed to enable fast and accurate prototyping and evaluation of virtual memory (VM) schemes.  It employs a lightweight userspace kernel, MimicOS, which imitates the desired OS kernel code, allowing researchers to simulate only the relevant OS routines and easily develop new OS modules.  Virtuoso's imitation-based OS simulation methodology facilitates the evaluation of hardware/OS co-designs by accurately modeling the interplay between OS routines and hardware components."}),"\n",(0,i.jsx)(t.h2,{id:"hardware-requirements",children:"Hardware Requirements"}),"\n",(0,i.jsxs)(t.ul,{children:["\n",(0,i.jsxs)(t.li,{children:[(0,i.jsx)(t.strong,{children:"Architecture"}),": x86-64 system."]}),"\n",(0,i.jsxs)(t.li,{children:[(0,i.jsx)(t.strong,{children:"Memory"}),": 4\u201313 GB of free memory per experiment."]}),"\n",(0,i.jsxs)(t.li,{children:[(0,i.jsx)(t.strong,{children:"Storage"}),": 10 GB of storage space for the dataset."]}),"\n"]}),"\n",(0,i.jsx)(t.h2,{id:"software-requirements",children:"Software Requirements"}),"\n",(0,i.jsxs)(t.ul,{children:["\n",(0,i.jsx)(t.li,{children:"Python 3.8 or later (Virtuoso+Sniper can also work with Python 2.7 but we do not recommend it)"}),"\n",(0,i.jsx)(t.li,{children:"libpython3.8-dev or later (make sure to install the correct version of libpython for your Python version)"}),"\n",(0,i.jsx)(t.li,{children:"g++"}),"\n"]}),"\n",(0,i.jsx)(t.p,{children:"We provide a script to install some potentially missing dependencies.\nThe will also install Miniconda, which is a lightweight version of Anaconda so that you can create a virtual environment for Python 3.8 or later."}),"\n",(0,i.jsx)(t.pre,{children:(0,i.jsx)(t.code,{className:"language-bash",children:"cd Virtuoso/simulator/sniper/\nsh install_dependencies.sh\nbash # start a new bash shell to make sure the environment variables are set\nconda create -n virtuoso python=3.10 \nconda activate virtuoso # Python 3.10 will be your default Python version in this environment\n"})}),"\n",(0,i.jsx)(t.p,{children:"Download some traces from workloads that experienced high address translation overheads."}),"\n",(0,i.jsx)(t.pre,{children:(0,i.jsx)(t.code,{className:"language-bash",children:"sh download_traces.sh\n"})}),"\n",(0,i.jsx)(t.h2,{id:"getting-started-1",children:"Getting Started"}),"\n",(0,i.jsx)(t.p,{children:"Let's first clean the Sniper simulator and then build it from scratch."}),"\n",(0,i.jsx)(t.pre,{children:(0,i.jsx)(t.code,{className:"language-bash",children:"cd Virtuoso/simulator/sniper\nmake distclean # clean up so that we can build from scratch\nmake -j # build Sniper\n"})}),"\n",(0,i.jsx)(t.p,{children:"Make sure the simulator is working by running the following command:"}),"\n",(0,i.jsx)(t.pre,{children:(0,i.jsx)(t.code,{className:"language-bash",children:"sh run_example.sh \n"})}),"\n",(0,i.jsx)(t.p,{children:"Let's now do something more interesting and run actual experiments.\nWe will run the following experiment:"}),"\n",(0,i.jsxs)(t.ol,{children:["\n",(0,i.jsx)(t.li,{children:"Baseline MMU with a Radix page table and a reservation-based THP policy (similar to the one used in FreeBSD) with 4KB and 2MB pages."}),"\n",(0,i.jsx)(t.li,{children:"We will sweep the memory fragmentation ratio to observe the effect of memory fragmentation on the performance of address translation."}),"\n",(0,i.jsx)(t.li,{children:"We will run the experiment multiple translation-intensive workloads."}),"\n"]}),"\n",(0,i.jsxs)(t.p,{children:["To run the experiments efficiently, we will use the Slurm job scheduler.\nIf you do not have Slurm installed, you can run the experiments by modifying the ",(0,i.jsx)(t.code,{children:"create_jobfile_virtuoso_reservethp.py"})," script to run the experiments sequentially without ",(0,i.jsx)(t.code,{children:"sbatch"})," and ",(0,i.jsx)(t.code,{children:"srun"}),"."]}),"\n",(0,i.jsx)(t.pre,{children:(0,i.jsx)(t.code,{className:"language-bash",children:"cd Virtuoso/scripts/virtuoso_sniper\npython3 create_jobfile_virtuoso_reservethp.py ../../Virtuoso/ ../jobfiles/reservethp.jobfile\ncd ../jobfiles\nsource reservethp.jobfile # This will run the experiments with Slurm\n"})})]})}function h(e={}){const{wrapper:t}={...(0,s.R)(),...e.components};return t?(0,i.jsx)(t,{...e,children:(0,i.jsx)(c,{...e})}):c(e)}},8453:(e,t,n)=>{n.d(t,{R:()=>o,x:()=>a});var r=n(6540);const i={},s=r.createContext(i);function o(e){const t=r.useContext(s);return r.useMemo((function(){return"function"==typeof e?e(t):{...t,...e}}),[t,e])}function a(e){let t;return t=e.disableParentContext?"function"==typeof e.components?e.components(i):e.components||i:o(e.components),r.createElement(s.Provider,{value:t},e.children)}}}]);