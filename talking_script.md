# Group 14 Presentation Script: Antarctic Microbial Sensor
**Presenter:** Saeed Solomon (with Group 14 team members supporting Q&A)  
**Total Target Duration:** 8 Minutes (480 Seconds) — Designed at a relaxed 130 words per minute (WPM) to allow for natural pauses and physical gestures, leaving a 40-second buffer.  
**Language:** Strict UK English (Visualisation, characterised, optimised, analysed, etc.)  

---

## 🎬 General Presentation Guidelines for Saeed
* **Tone:** Confident, enthusiastic, and conversational. You are telling a story of engineering resilience—how four students solved a massive scientific challenge under a strict budget of R2,000.
* **Pacing:** Avoid rushing. Use the timing cues below to stay on track. If you are ahead of time, take deep breaths and speak more slowly during the subsystem slides.
* **Body Language:** Use your hands to point out elements on the screen. The script contains explicit **[Visual Cues]** telling you exactly where to look or point.

---

### Slide 1: Title Slide
* **Duration:** 30 Seconds (Cumulative: 0:30)
* **Slide ID:** `slide-1` (Antarctic Microbial Sensor)
* **Visual Cues:** Look at the audience, smile, and gesture towards the main title on the screen behind you.
* **Talking Script:**
  > "Good afternoon, everyone. My name is Saeed Solomon, and on behalf of Group 14—including my teammates Kiyuran, Devon, and Joshua—I am incredibly excited to present our engineering systems design project: the Antarctic Microbial Sensor. 
  > 
  > Our project is all about bridging the winter data gap in Southern Ocean observation. Over the next eight minutes, I am going to walk you through how we designed, built, and validated a robust, autonomous microbial sensor system that operates under extreme polar conditions on a tiny budget of less than two thousand Rands. Let us dive in."

---

### Slide 2: Stakeholder & Context
* **Duration:** 45 Seconds (Cumulative: 1:15)
* **Slide ID:** `slide-2` (Stakeholder & Context)
* **Visual Cues:** **[POINT]** to the top-left card showing **Dr Emma Rocke's** profile. Then, **[SWIPE HAND]** towards the **Research Context** bullet points on the glass card on the right.
* **Talking Script:**
  > "To understand why we built this system, we must first meet our primary stakeholder: Dr Emma Rocke, a leading marine microbiologist at UCT's MARiS Institute. Emma's research focuses on marine microbes, which she beautifully describes as 'the invisible engines of the ocean.' These microscopic organisms are critical early indicators of global climate shift and ecosystem disruption. 
  > 
  > However, scientific data collection in the Southern Ocean is severely constrained. Standard research cruises are incredibly expensive and infrequent. Most importantly, when the harsh Antarctic winter sets in, freezing sea ice and severe polar storms make it physically impossible for ships to operate. This creates a massive winter observation gap—several consecutive months where marine biologists have zero visibility into what is happening beneath the ice."

---

### Slide 3: The Problem
* **Duration:** 45 Seconds (Cumulative: 2:00)
* **Slide ID:** `slide-3` (The Problem)
* **Visual Cues:** **[GESTURE]** to the three warning-gold glass cards representing the core challenges: **The Winter Gap**, **Human Risk**, and **Incomplete Data**.
* **Talking Script:**
  > "This brings us to the core problem. The winter gap means that scientists miss the critical early-warning signals of ecological changes, such as harmful algal blooms. 
  > 
  > Furthermore, the traditional method of deploying research gear in sub-zero temperatures involves extreme human risk. Researchers face physical danger and severe psychological strain working in icy, gale-force winds to gather water samples manually. 
  > 
  > Our mission was clear: we needed to engineer an autonomous, hand-deployable, and highly resilient submersible sensor probe. This device had to survive the freezing depths, gather continuous high-resolution biological profiles, and safely offload the data without exposing researchers to the freezing cold."

---

### Slide 4: Our Solution — Engineering Subsystems
* **Duration:** 40 Seconds (Cumulative: 2:40)
* **Slide ID:** `slide-4` (Our Solution — Engineering Subsystems)
* **Visual Cues:** **[SWEEP HAND]** across the four glass cards representing **Edge AI**, **Power**, **Data Link**, and **Sensors**. Point out the icons as you name them.
* **Talking Script:**
  > "To tackle this massive challenge, we broke the design down into four core engineering subsystems, each managed by a team member.
  > 
  > First, **Edge AI**—developed by Kiyuran—which performs unsupervised machine learning and explainable AI directly on the host laptop to isolate anomalies.
  > Second, the **Power Subsystem**—engineered by Devon—which uses specialized lithium battery chemistry optimized to survive sub-zero temperatures.
  > Third, the **Data Link**—which I developed—which leverages connectionless wireless telemetry to transfer data through a sealed hull.
  > And fourth, the **Sensors and Housing**—designed by Joshua—incorporating an 11-channel optical fluorometer inside a rugged PVC pressure vessel. 
  > Let us look at how these hardware and software components come together."

---

### Slide 5: Our Solution — Hardware & Software (Bipartite Platform)
* **Duration:** 45 Seconds (Cumulative: 3:25)
* **Slide ID:** `slide-5` (Our Solution — Hardware & Software)
* **Visual Cues:** **[POINT]** to the left-hand images of the physical **Submersible Probe**. Then, **[POINT]** to the right-hand image showing the **Mission Control GUI** dashboard.
* **Talking Script:**
  > "Our solution is characterised by a bipartite platform—meaning it is split into two distinct elements that work in perfect harmony.
  > 
  > On the left, you can see our physical hardware: the **Submersible Probe**. This is a completely sealed, pressure-rated instrumentation tube designed to be lowered into the icy water. It has no external physical data ports, preventing any water ingress.
  > 
  > On the right, you can see our software: the **Mission Control GUI**. This is a high-performance offline dashboard that runs on a scientist's laptop. When the probe returns from a dive, it communicates wirelessly with this dashboard to instantly transmit and visualise the logged profiles. This dual architecture ensures the hardware remains watertight while scientists get rich, interactive data."

---

### Slide 6: How to Deploy the Probe
* **Duration:** 30 Seconds (Cumulative: 3:55)
* **Slide ID:** `slide-6` (How to Deploy the Probe)
* **Visual Cues:** **[SMILE]** and use a lighter, humorous tone. **[POINT]** to the green-bordered card on the left (**The Right Way**), then **[POINT]** to the red-bordered card on the right (**The Wrong Way**).
* **Talking Script:**
  > "Now, before we discuss the deep technical details, let us establish some quick field rules for deployment! 
  > 
  > As you can see in the green card on the left, the **Right Way** to deploy our probe is to gently lower it on a secure marine rope from the side of the research vessel or ice shelf. It is designed to descend smoothly and vertically.
  > 
  > On the right... well, we must emphasize that this is not a hammer throw competition! Throwing the probe is highly discouraged. Although the housing is extremely rugged, we do want Dr Rocke to get her sensor back in one piece! So, gentle rope lowering is the golden rule."

---

### Slide 7: Subsystem 1 — Data Transfer & Visualisation
* **Duration:** 45 Seconds (Cumulative: 4:40)
* **Slide ID:** `slide-7` (Subsystem 1 — Data Transfer & Visualisation)
* **Visual Cues:** **[POINT]** to the **Wireless Transfer Link** card on the left. Then, **[POINT]** to the **Subsystem Metrics** grid on the right, highlighting the **1.2% Error Rate** and **30 m Range**.
* **Talking Script:**
  > "Let us examine Subsystem 1: Data Transfer and Visualisation. The primary engineering challenge here was retrieving data from a completely sealed probe.
  > 
  > We implemented the connectionless **ESP-NOW wireless protocol**. When the probe wakes up, it advertises and broadcasts data packets directly to a custom USB receiver dongle plugged into the host laptop. This connect-free mechanism takes less than 60 seconds to offload an entire mission's data, eliminating the need for physical waterproof bulkheads.
  > 
  > On the laptop, our **CustomTkinter GUI** dashboard automatically parses, logs, and exports this data to CSV files. Looking at our empirical metrics on the right, we achieved a brilliant telemetry packet error rate of just **1.2%** over a wireless range of **30 metres**, far exceeding our design contract."

---

### Slide 8: Subsystem 2 — Edge Architecture & Explainable AI
* **Duration:** 45 Seconds (Cumulative: 5:25)
* **Slide ID:** `slide-8` (Subsystem 2 — Edge Architecture & Explainable AI)
* **Visual Cues:** **[POINT]** to the **Low-Power Firmware** card, and then **[GESTURE]** towards the **ML Accuracy (92%)** stat box on the right.
* **Talking Script:**
  > "Subsystem 2 focuses on the Edge AI and firmware lifecycle. The probe runs a highly deterministic, low-power state machine: STANDBY, ARMED, LOGGING, and OFFLOAD. During deep sleep, all logging records are cached directly in the RTC SRAM to survive microprocessor resets, keeping standby draw to a tiny **16.8 micro-Amps**.
  > 
  > The core software on the laptop runs a hybrid AI classifier. An unsupervised **Isolation Forest** algorithm first flags any anomalous readings in the temperature, pressure, or spectral bands. 
  > 
  > Then, a post-hoc **Explainable AI ruleset** interprets these anomalies, categorising them into either real biological events—like a localized phytoplankton bloom—or physical sensor noise, such as passing ice bubbles. This achieves an outstanding **92% ML accuracy** in under **8 milliseconds** of inference time."

---

### Slide 9: Subsystem 3 — Power & Thermal
* **Duration:** 45 Seconds (Cumulative: 6:10)
* **Slide ID:** `slide-9` (Subsystem 3 — Power & Thermal)
* **Visual Cues:** **[POINT]** to the **Battery & Split-Rail Power** card. Gesture to the **365-day Polar Mission Life** statistic at the bottom right.
* **Talking Script:**
  > "Subsystem 3 is our power and thermal core. Operating in sub-zero water requires extreme thermal resilience. 
  > 
  > We utilised polar-grade **Lithium-Thionyl Chloride chemistry** cells—specifically ER14505 batteries rated down to minus 55 degrees Celsius. These are buffered by a **1 Farad supercapacitor** to handle the high current spikes required during wireless ESP-NOW data bursts. To save power, we designed a split-rail power system: a low-dropout regulator handles always-on tasks, while the main sensor rail is completely isolated via a P-MOSFET switch during sleep.
  > 
  > Thermally, the internal electronics are cocooned in a **passive insulation shield** of layered foil and high-density foam. This traps the regulator's waste heat, keeping the batteries warm. Under cold-chamber tests, our thermal huddle maintained the core temperature **18 degrees above ambient**, securing a full **1-year polar mission life**."

---

### Slide 10: Subsystem 4 — Sensors & Housing
* **Duration:** 45 Seconds (Cumulative: 6:55)
* **Slide ID:** `slide-10` (Subsystem 4 — Sensors & Housing)
* **Visual Cues:** **[POINT]** to the **Chlorophyll Fluorometer** card, then **[POINT]** to the **Housing** card. Highlight the **Waterproof Target** and **Depth Error** metrics.
* **Talking Script:**
  > "Finally, let us look at Subsystem 4: Sensors and Housing. To capture microbial concentrations, we integrated the **AS7341 11-channel spectral sensor**. By driving a custom, time-varying LED PWM excitation ramp, the sensor excites chlorophyll molecules and measures the resulting fluorescence emission. Our tests proved this can resolve extremely low concentrations down to a **1/32 spinach dilution**!
  > 
  > This delicate optical core is protected by a heavy-duty **50 mm rigid PVC chassis**. The housing is sealed using double friction-fit rubber O-rings with a carefully calculated **17% compression rate** to ensure absolute watertightness. 
  > 
  > Finally, an integrated **250 gram lead ballast** is added to the bottom, ensuring the probe achieves negative buoyancy and descends vertically at a reliable, steady rate."

---

### Slide 11: Verification — All Goals Met
* **Duration:** 40 Seconds (Cumulative: 7:35)
* **Slide ID:** `slide-11` (Verification — All Goals Met)
* **Visual Cues:** **[SWEEP HAND]** across the entire 6-card grid. Nod confidently and look at the audience, making eye contact.
* **Talking Script:**
  > "To prove our system design was successful, we subjected the entire prototype to a rigorous empirical verification matrix.
  > 
  > As you can see on this slide, we exceeded every single design contract target. Our data link error margin is just **1.2%** compared to the 5% limit. We achieved a wireless range of **30 metres**, tripling our 10-metre target. Our wake latency is only **8 seconds**, our sleep current is a microscopic **16.8 micro-Amps**, and our machine learning model processes records in just **8 milliseconds**.
  > 
  > These are not just simulation figures—these are real, physical testing results that prove our system is ready for the field."

---

### Slide 12: Mission Accomplished
* **Duration:** 25 Seconds (Cumulative: 8:00)
* **Visual Cues:** **[GESTURE]** towards the closing slogan on the screen, then look at the panel of examiners. **[BOW SLIGHTLY]** to indicate completion.
* **Talking Script:**
  > "In conclusion, Group 14 has successfully engineered a fully autonomous, low-power, and extremely rugged microbial sensor system on a budget of under two thousand Rands. By bridging the winter data gap, we can empower researchers like Dr Emma Rocke to monitor polar oceans continuously and safely, one dive at a time.
  > 
  > Thank you so much for your time. My team and I are now open to any questions you may have."

---

## 🙋 Q&A Support Guide for the Teammates
*If the examiners ask specific subsystem questions, Saeed can hand over to the respective expert:*
* **Data Transfer & GUI (Subsystem 1):** Saeed Solomon (ESP-NOW, serial packet structure, CustomTkinter thread handling).
* **Edge AI & Firmware (Subsystem 2):** Kiyuran Naidoo (Isolation Forest math, explainable ruleset, RTC SRAM caching, state-machine states).
* **Power & Thermal (Subsystem 3):** Devon Clark (Li-SOCl₂ discharge curves, split-rail P-MOSFET math, passive thermal insulation coefficients).
* **Sensors & Housing (Subsystem 4):** Joshua Naidoo (AS7341 spectral channels, LED excitation PWM control, PVC pressure calculations, O-ring compression mechanics).
