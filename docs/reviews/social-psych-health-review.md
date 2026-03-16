# Social Psychology Review: CYT-NG Handheld Health Features

**Reviewer perspective:** Social psychologist with focus on interpersonal violence, technology-facilitated abuse, and the psychological effects of surveillance and counter-surveillance.

**Date:** 2026-03-16

**Scope:** Review of the CYT-NG handheld's pivot to a genuine dual-purpose device: real personal wellness tracker (pedometer, cycle tracker, mood tracker, sobriety counter) with background surveillance detection. This review builds on my previous review of the CYT-NG base station and handheld architecture.

**Previous review:** `docs/reviews/social-psych-review.md` in the CYT-NG base station repository.

---

## 0. The Fundamental Shift and Why It Matters

The previous CYT-NG handheld was a surveillance detection device that could pretend to be a fitness tracker. The new direction inverts this: it IS a wellness device that also runs surveillance detection in the background. This is not a cosmetic change. It is a structural transformation of the user's psychological relationship to the device.

In my previous review, I identified hypervigilance amplification as the primary psychological risk: a device that asks you to constantly monitor for threats will make you more anxious, not less. The new architecture partially addresses this by giving the user a reason to interact with the device that is not threat-related. You pick it up to check your steps, log your mood, track your cycle. The surveillance detection runs silently underneath, surfacing only when it has something meaningful to report.

This is a genuinely better design. But it introduces a new set of psychological dynamics that require careful analysis.

---

## 1. Psychological Effects of the Dual-Purpose Device

### 1.1 The Contamination Problem

When a single object serves two psychologically distinct functions -- wellness and threat detection -- the emotional associations of each function can bleed into the other. This is a well-documented phenomenon in environmental psychology: objects and spaces acquire emotional valence through association, and that valence generalizes (Mehrabian & Russell, 1974).

**Scenario:** A user checks their pedometer. 8,400 steps today. A small moment of satisfaction. Then a notification appears: a BLE device has been detected at three of their locations. The pedometer screen, which was a source of minor positive affect five seconds ago, is now associated with threat. Over repeated pairings, the act of checking steps becomes a cue for anxiety. The user stops checking steps. The wellness features atrophy. The device reverts to being experienced as a threat detector.

This is classical conditioning operating at the level of device interaction. The wellness features (neutral or positive stimulus) are paired with surveillance alerts (aversive stimulus). Over time, the wellness features themselves acquire aversive properties.

**Counterargument:** The contamination risk is real, but the alternative is worse. A device that does ONLY surveillance detection provides no positive associations at all. Every interaction is threat-related. The wellness features provide a psychological anchor -- something the user does with the device that is normal, mundane, and unrelated to being surveilled. Even if some contamination occurs, the net psychological effect is likely better than a pure threat-detection device.

**Recommendation:** Architecturally separate the two functions in the UI. Health screens should never display surveillance alerts. Surveillance alerts should never appear while the user is actively interacting with a health feature. If a detection occurs while the user is logging their mood, queue the notification until they leave the health screens. The health space on the device should feel psychologically safe.

### 1.2 The Grounding Function

There is a positive psychological dynamic at work here that should not be underestimated. For stalking victims experiencing chronic threat, daily routines become disrupted. Normal activities feel impossible or meaningless. A device that asks you to count your steps, track your cycle, and log your mood is, at a basic level, asking you to maintain the structure of a normal life. It is a behavioral activation intervention delivered through a wearable device.

Behavioral activation -- the deliberate scheduling and performance of routine and pleasurable activities -- is one of the most effective interventions for depression and anxiety (Dimidjian et al., 2006). The pedometer says "you walked today." The mood tracker says "you checked in with yourself today." The cycle tracker says "your body is still doing its thing." These are small anchors of normalcy in a life that may feel dominated by threat.

**Recommendation:** Lean into this. The health features should feel warm, personal, and encouraging. Not clinical, not gamified. Simple acknowledgments: "You walked 6,200 steps today." Not "Great job! You're 62% of the way to your goal!" Stalking victims do not need a cheerful fitness coach. They need a quiet confirmation that they are still living their life.

### 1.3 The Mood Tracker as Anxiety Source

The mood tracker asks the user to quantify their emotional state on a 1-10 scale. This is standard in clinical self-monitoring, but it carries specific risks for this population.

**Risk 1: Forced confrontation with distress.** A user who is coping through avoidance (a common and sometimes adaptive strategy in ongoing threat situations) is forced to look directly at their emotional state. If the answer is consistently "2" or "3," the mood tracker becomes a daily reminder that they are suffering. The `needs_checkin` flag (true if >24h since last log) adds gentle pressure to perform this confrontation daily.

**Risk 2: Performance anxiety.** Some users will feel pressure to log "good" moods, either because they do not want to see a declining trend or because they know the data might eventually be shared with a therapist, attorney, or court. This produces inauthentic data and adds stress to an interaction that should reduce it.

**Risk 3: Decline awareness.** The 7-day trend feature (MOOD_TREND_DECLINING) makes mood deterioration visible and quantified. For some users, seeing a declining trend is motivating -- it prompts them to seek help. For others, it is demoralizing -- "things are getting worse and now there's a graph proving it."

**Recommendation:** Make the mood check-in optional and frame it gently. Remove the `needs_checkin` pressure mechanism or make it opt-in. When displaying trends, avoid value judgments. "Your mood over the past week" is better than "Your mood is declining." If the trend is negative, pair it with a resource: "If you'd like to talk to someone, [resource]." Never make the user feel evaluated by their own device.

---

## 2. Evidence Correlation: Psychological and Legal Analysis

### 2.1 Mood-Surveillance Correlation as Evidence

The idea of correlating mood logs with surveillance detections for court evidence is clever and has genuine forensic value. A timeline showing: "Device X detected at four locations on March 3; user's mood score dropped from 7 to 3 over the following five days" tells a compelling story about the psychological impact of stalking.

However, this correlation is a double-edged instrument.

**Prosecution use:** "The defendant's surveillance of the victim caused measurable psychological harm. The victim's self-reported mood declined in direct temporal relationship to detection events."

**Defense use:** "The complainant's mood data shows chronic depression and anxiety predating any alleged surveillance. The mood decline on March 3 is consistent with the complainant's baseline instability, not my client's behavior. Furthermore, the complainant was using a surveillance detection device, which itself induces anxiety (see: reviewer's own previous report on hypervigilance amplification). The mood decline is caused by the device, not my client."

The defense argument has merit. My previous review explicitly warned that surveillance detection tools can amplify anxiety. A defense attorney who reads that review will use it. The mood data becomes a weapon against the victim.

**Recommendation:** Do NOT automatically link mood entries to surveillance events in stored data or exports. Keep the two data streams independent on the device. Let the user's attorney, therapist, or advocate construct the correlation narrative in the appropriate legal context. The device should store timestamped mood logs and timestamped surveillance events separately. A human analyst can draw connections; the device should not pre-draw them, because pre-drawn correlations are easier to attack.

### 2.2 Cycle Tracker Data in Court

This is where the privacy implications become extreme and I want to be direct about it.

Menstrual cycle data is among the most intimate health information a person can generate. In the post-Dobbs legal landscape in the United States, cycle tracking data has already been discussed as potential evidence in abortion-related prosecutions. Putting cycle data on a device that may be examined by law enforcement, entered into evidence in a court proceeding, or seized during a domestic dispute creates risks that go far beyond the stalking context.

Consider: a stalking victim's device is seized as evidence in a protection order hearing. The device contains cycle tracking data showing irregular periods, which could indicate pregnancy, miscarriage, or abortion. This data is now in the court record. In states with restrictive reproductive laws, this data could become the basis for a separate investigation. Even in states without such laws, the data is now accessible to the opposing party's attorney, who may be the abuser's attorney.

The argument that cycle disruption (stress-induced amenorrhea, irregular cycles) corroborates the psychological impact of stalking is medically sound. Chronic stress does disrupt menstrual cycles (Nagma et al., 2015). But the evidentiary value does not justify the privacy risk.

**Recommendation:** The cycle tracker should be the MOST protected data on the device. Specific measures:
- Cycle data should be encrypted with a separate key, not just the device's general encryption.
- Cycle data should NOT be included in any forensic export unless the user explicitly, separately consents to its inclusion.
- During a duress wipe, cycle data should be destroyed. It is too intimate to risk exposure.
- The companion app should allow cycle data to be synced to a separate, user-controlled location (their own phone, a personal health app) and deleted from the device entirely.
- If cycle data is used for evidence, it should be provided directly by the victim to their own attorney, never exported as part of a general device data dump.

### 2.3 The Export Format Question

Health data exports for therapy, medical, or legal use should be in a standard, interoperable format. For medical purposes, FHIR (Fast Healthcare Interoperability Resources) is the standard, but it is absurdly complex for a three-button embedded device. A more practical approach:

- **CSV with clear headers and metadata** for legal/advocacy use. Timestamps, values, no device identifiers.
- **JSON for programmatic consumption** by the companion app or a therapist's tools.
- **The user must explicitly choose** what categories to include in any export: steps, mood, cycle, sobriety -- each independently selectable.
- **Exports should be signed** (device-key signature) to establish provenance but should NOT contain device serial numbers or surveillance detection data unless separately selected.

---

## 3. The Sobriety Counter: Benefit and Risk

### 3.1 Why It Helps

Substance abuse and addiction are disproportionately prevalent among DV/stalking survivors. The relationship between victimization and substance use is bidirectional and well-documented: trauma increases substance use risk, and substance use increases vulnerability to further victimization (Kilpatrick et al., 2003). A sobriety counter on a device the user carries daily provides:

- **Daily reinforcement of recovery.** Seeing "Day 47" every time you check the time is a small but meaningful anchor.
- **Milestone recognition.** The milestone system (30 days, 60 days, 90 days) mirrors the structure of 12-step programs and provides a sense of progress.
- **Integration with the wellness identity.** "I am a person who tracks my health" is a more empowering identity than "I am a person who monitors threats." The sobriety counter reinforces the former.

### 3.2 Why It Is Dangerous

The sobriety counter is the single most dangerous piece of data on this device if it is discovered by the wrong person.

**If the stalker/abuser finds it:** An abuser who discovers their victim is in recovery gains leverage. Sobriety is a vulnerability in abusive relationships. The abuser may:
- Use the information to undermine recovery ("I know you're trying to stay sober, so let me bring alcohol to every interaction").
- Weaponize relapses ("You reset your counter -- you're an addict, you can't be trusted, no court will believe you").
- Use the information in custody proceedings ("The other parent is a recovering addict" -- true, but presented to cause harm).

**If the device is seized by law enforcement:** A sobriety counter reveals a history of substance abuse. This information can affect how law enforcement treats the victim (bias against addicts is well-documented), how a court weighs their testimony, and how a defense attorney frames their credibility.

**If the device is examined in any legal proceeding:** The sobriety counter's start date implies a period of substance abuse before that date. A reset implies a relapse. This is deeply personal information that the user may not want in the legal record.

### 3.3 Recommendations for the Sobriety Counter

- **The sobriety counter should be hidden by default.** It should not appear in any menu unless the user explicitly enables it. A user who does not use this feature should leave no trace that it exists.
- **Sobriety data should be the first thing destroyed in a duress wipe.** Before surveillance data, before mood data, before anything else. The risk/benefit calculation is clear: a sobriety start date has minimal evidentiary value for stalking prosecution but maximal potential for harm to the victim.
- **The counter should store only the start date.** No history of resets, no log of previous attempts. The current implementation (`sobriety_status_t` stores only `start_date`) correctly minimizes data. Do not add features that log reset history.
- **Consider making the sobriety counter available only through the phone companion,** not on the device itself. This way, the data lives on the user's personal phone (which they control and can wipe) rather than on a device that might be seized or discovered.

---

## 4. Feature Discovery Risk

### 4.1 Mood Data as Stalker Intelligence

If a stalker discovers the device and accesses the mood tracker, declining mood scores provide feedback on the effectiveness of their harassment campaign. This is not a theoretical concern. Research on stalking motivation consistently identifies a desire for control and evidence of impact as primary motivators (Mullen et al., 2000). A mood tracker showing a decline from 7 to 2 over three weeks is, to a stalker, a progress report.

The `mood_trend_t` enum (IMPROVING / STABLE / DECLINING) makes this even easier to parse. A stalker does not need to interpret raw numbers; the device has already labeled the trend for them.

**Recommendation:** Mood data should require a PIN or biometric to access, separate from the device's general unlock. If the device is accessed without this secondary authentication, the mood tracker should show no data -- not a locked screen (which reveals the feature exists), but simply an empty/unconfigured state.

### 4.2 Cycle Data Exposure

Everything said about mood data applies with greater force to cycle data. The cycle tracker reveals:
- Whether the user is menstruating (current cycle day).
- Whether they might be pregnant (missed period, irregular cycle).
- Intimate details of reproductive health (BBT logging, fertile window).

If a cohabiting abuser discovers this data, it can be used for reproductive coercion -- a documented form of intimate partner violence where the abuser controls the victim's reproductive decisions (Miller et al., 2010).

**Recommendation:** Cycle data should have the strongest protection of any feature on the device. Separate encryption key, separate PIN, no indication of its existence in the default UI. If the device is in stealth mode, the cycle tracker should not be accessible at all -- the data exists only in encrypted NVS and can only be viewed through the phone companion.

### 4.3 Health Data Protection Architecture

The current design encrypts health data "when stealth mode is active." This is insufficient. Health data is sensitive at all times, not only during stealth operation.

**Recommendation:** Health data should be encrypted at rest by default, not only in stealth mode. The encryption key should be derived from a user-set PIN that is separate from any device-level password. This ensures that even if the device is physically accessed, health data remains protected.

The layered protection model should be:
1. **Pedometer data:** Low sensitivity. Encrypted at rest, but accessible without secondary authentication. Steps are not revealing.
2. **Mood data:** Medium-high sensitivity. Encrypted at rest, requires PIN to view.
3. **Sobriety counter:** High sensitivity. Encrypted at rest, requires PIN, hidden feature, destroyed on duress wipe.
4. **Cycle data:** Highest sensitivity. Encrypted with separate key, requires PIN, not accessible in stealth mode, destroyed on duress wipe, not included in default exports.

---

## 5. Framing, Language, and Identity

### 5.1 What Should the Device Call Itself?

The name matters because it shapes the user's psychological relationship to the device and determines how it is perceived if discovered.

Options and analysis:

| Name | Pros | Cons |
|------|------|------|
| "Personal Wellness Tracker" | Accurate, benign, unremarkable | Slightly corporate/clinical |
| "Health Monitor" | Simple, clear | Could imply medical device (regulatory concern) |
| "Daily Wellness" | Warm, personal | Vague |
| "My Wellbeing" | First-person, personal | Slightly precious |
| "[User's name]'s Health" | Personal, natural | Requires name entry |

**Recommendation:** "Personal Wellness Tracker" or simply the user's chosen device name. The boot screen, if any, should show something like a step count or the current date -- unremarkable content that does not invite curiosity. Avoid any name that suggests monitoring, tracking (in the surveillance sense), or security.

### 5.2 The Ethics of Hidden Functionality

The device hides its surveillance detection capability. Is this ethical?

The answer depends on who is being deceived and why.

**The user is not deceived.** They purchased/built the device knowing its full capability. Informed consent is satisfied.

**A stalker who discovers the device is deceived.** They see a wellness tracker, not a counter-surveillance tool. This deception is defensive and proportionate. The victim is under no ethical obligation to reveal their safety measures to the person threatening them. This is analogous to a hidden panic button or a discreet security camera -- the deceptive presentation serves a protective function.

**Law enforcement who find the device may be deceived.** This is more complex. If a police officer examines the device during a welfare check or a traffic stop, they see a health tracker. The surveillance detection data is hidden. Is this obstruction? In most jurisdictions, no -- there is no affirmative duty to reveal the full capabilities of a personal electronic device. But if the device is seized pursuant to a warrant, the hidden functionality could be perceived as an attempt to conceal evidence or capabilities, which could affect the user's credibility.

**Recommendation:** The device should have a "disclosure mode" that the user can activate when voluntarily sharing the device with law enforcement or an attorney. This mode reveals the full functionality and data in an organized, professional format. The user chooses when and whether to activate it. The hidden default is for safety; disclosure is for legal contexts.

### 5.3 Law Enforcement Encounter Protocol

When law enforcement encounters the device:

1. **Casual inspection (traffic stop, welfare check):** Device presents as a wellness tracker. This is appropriate. The user has no obligation to volunteer its surveillance detection capability.
2. **Voluntary sharing with police (filing a stalking report):** User activates disclosure mode. Device presents surveillance detection data in a clean, exportable format alongside health data that the user chooses to include.
3. **Warrant/seizure:** The device's encryption protects health data. Surveillance detection data may be accessible depending on encryption implementation. The existence of hidden functionality will eventually be discovered by forensic examination. This is acceptable -- the user's purpose is defensive, and forensic examiners will understand the context.

---

## 6. Missing Features and Features to Avoid

### 6.1 Features That Should Be Added

**Safety check-in timer.** This is the highest-priority missing feature. "If I don't check in by [time], send an alert to [contact]." For stalking victims, especially those in active danger, a dead-person switch is potentially life-saving. Implementation via BLE to phone companion, which sends the alert via the phone's cellular connection.

The psychological benefit is also significant: knowing that someone will be alerted if you do not check in reduces the feeling of isolation that is central to the stalking experience. "Someone will notice if something happens to me" is a powerful anxiolytic.

Design considerations:
- The check-in should be simple: a single button press on the device.
- The reminder should be gentle and private: a vibration, not a screen alert.
- The escalation should be configurable: first a text to a trusted contact, then a call, then (optionally) emergency services.
- False alarm recovery should be easy: a late check-in immediately sends "I'm OK" to the contact.

**Sleep quality estimation.** The QMI8658 accelerometer can detect movement during sleep (actigraphy). Sleep disruption is one of the most reliable indicators of psychological distress and is directly relevant to documenting the impact of stalking. It also does not require active user input, reducing the burden on the user.

Sleep data is lower sensitivity than mood or cycle data (knowing someone slept poorly is not weaponizable in the way mood scores or cycle data are) and has clear therapeutic and evidentiary value.

**Emergency contact quick-action.** A hardware button combination (e.g., triple-press of the MODE button) that triggers an emergency action via the phone companion: send GPS coordinates and a pre-set message to an emergency contact. This should work even if the device screen is off or in stealth mode.

**Breathing exercise / grounding timer.** A simple 4-7-8 breathing exercise or box breathing timer accessible from the health menu. This is a genuine micro-intervention for acute anxiety. It takes zero data storage, requires no privacy protection, and reinforces the device's identity as a wellness tool. For a user who has just received a surveillance alert, having a grounding exercise on the same device provides an immediate coping resource.

### 6.2 Features to Avoid

**Journaling / free-text diary.** Free-text entries are the most dangerous data type on a device like this. They can contain admissions, plans, emotional outbursts, references to other people -- all of which can be weaponized in legal proceedings or by an abuser. The mood tracker's 1-10 scale is appropriately constrained. Do not add free text.

**Social features / peer comparison.** "Sarah walked 10,000 steps today!" Any feature that connects users to each other creates deanonymization risk and social pressure. Do not build community or social features into the device. (This is consistent with my previous review's recommendation.)

**Gamification / streaks / achievements.** Gamification creates psychological pressure to engage, which can become a source of stress. A stalking victim who "breaks their streak" by not logging mood for two days does not need the additional guilt of losing a virtual badge. The sobriety counter's milestones are an exception because milestone counting is intrinsic to recovery culture and expected by users.

**Heart rate / SpO2 monitoring.** The hardware does not support it (no optical sensor), but even if it did: physiological stress indicators that the user cannot control (elevated heart rate during a panic attack) create data that could be used to question the user's emotional stability. Stick to behavioral measures the user actively logs.

**Location history on the device itself.** GPS data is used for surveillance detection and should be handled by that subsystem. The health features should not maintain an independent location history (route maps, location-tagged mood entries). Location data is surveillance data; keep it out of the health layer.

---

## 7. Data Retention, Export, and the Duress Wipe Dilemma

### 7.1 The Core Tension

A duress wipe destroys data to protect the user from an immediate physical threat (an abuser demanding to see the device). But health data is also medical records that may be needed for therapy, legal proceedings, or personal reference. Destroying health data during a duress wipe means destroying potential evidence and personal medical history.

This is a genuine dilemma with no clean answer. Here is the analysis:

**Arguments for destroying health data in a duress wipe:**
- If the device is being examined under duress, the examiner is hostile. Any data on the device can be used against the user.
- A "clean" device after a duress wipe is more plausible if it contains no data at all. A device with health data but no surveillance data looks like selectively wiped evidence.
- Cycle and sobriety data are more dangerous to the user than they are valuable as evidence.

**Arguments for preserving health data through a duress wipe:**
- Health data is the user's medical record. Destroying it without consent is a serious action.
- If the device is supposed to be "just a wellness tracker," having health data after a wipe supports the cover story. An empty wellness tracker is suspicious.
- Step counts and basic mood logs are not dangerous and could help maintain the device's innocent appearance.

**Recommendation:** Implement a tiered duress wipe:

| Data category | Duress wipe behavior | Rationale |
|---------------|---------------------|-----------|
| Surveillance detection data | Destroyed | Primary threat vector |
| Familiar device database | Destroyed | Reveals counter-surveillance activity |
| Sobriety counter | Destroyed | High vulnerability risk |
| Cycle tracker data | Destroyed | Extreme privacy sensitivity |
| Mood tracker history | Destroyed | Could reveal distress, be weaponized |
| Pedometer (last 7 days) | PRESERVED | Innocuous; supports cover identity |
| Device settings/config | PRESERVED | A factory-reset device is suspicious |

After a duress wipe, the device should look like a wellness tracker that the user has been using casually -- some step data, default settings, no sensitive health data, no surveillance data. This is more plausible than a completely empty device.

### 7.2 Backup Before Wipe

The companion app should maintain encrypted backups of all health data on the user's phone. If a duress wipe occurs, the health data can be recovered from the phone backup (which the user controls and can protect separately). This means the user loses nothing permanently by triggering a duress wipe -- the data is gone from the device but safe on their phone.

**Critical implementation detail:** The backup must happen automatically and regularly, not only when the user initiates it. A user who needs to trigger a duress wipe will not have time to perform a manual backup first.

### 7.3 Export Controls

When exporting health data for therapy, legal, or medical use:

- **The user must explicitly select each data category for export.** No "export all" button. Each category (steps, mood, cycle, sobriety) has its own checkbox.
- **Surveillance detection data is NEVER included in health exports.** These are separate export functions.
- **The user must choose whether mood entries are timestamped** in the export. Timestamps enable correlation with surveillance events (useful for legal purposes) but also reveal when the user was interacting with the device (potentially revealing to an adversary reviewing the export).
- **Exports should include a metadata header** identifying the data source, date range, and a statement that the data was self-reported and not clinically validated.

---

## 8. Revised Ethical Framework

My previous review proposed six ethical principles. The health feature integration requires updates to two of them and the addition of a seventh:

**Updated Principle 2 (Informed Autonomy):** The user should have full control over what the tool does, when it does it, what it tells them, AND what personal health information it retains. Health data collection should be opt-in per feature. The user should be able to use the pedometer without enabling the mood tracker, or vice versa. No feature should silently generate health data the user did not request.

**Updated Principle 3 (Minimal Data Retention):** Health data and surveillance data should follow different retention policies. Surveillance data should decay aggressively (48-hour default). Health data should be retained as long as the user wants it (it is their medical record) but should be destroyable instantly via duress wipe. The two data types have different risk profiles and different value propositions.

**New Principle 7 (Health Data Dignity):** The device handles intimate health information -- menstrual cycles, mental health status, addiction recovery. This data deserves the same respect and protection that a medical provider would give it, even though the device is not a medical device. This means: encryption at rest, access controls, no unauthorized sharing, user-controlled exports, and the ability to completely and irrecoverably destroy the data at any time. The user's health data belongs to the user. It is not evidence, not a feature, not a data point. It is a person's private medical history, and the device is a temporary custodian.

---

## 9. Summary of Recommendations

### High Priority

| # | Recommendation | Rationale |
|---|---------------|-----------|
| 1 | Architecturally separate health UI from surveillance alerts -- no cross-contamination | Prevents classical conditioning of wellness features with threat associations |
| 2 | Do NOT auto-link mood entries to surveillance events in storage or export | Prevents defense attorney weaponization; let humans draw correlations |
| 3 | Cycle data gets strongest protection: separate encryption key, separate PIN, excluded from default exports, destroyed on duress wipe | Extreme privacy sensitivity in current legal climate |
| 4 | Sobriety counter hidden by default, destroyed first on duress wipe, no reset history stored | High vulnerability to weaponization by abuser or opposing counsel |
| 5 | Add safety check-in timer ("dead person switch") | Highest-value missing feature for this population |
| 6 | Tiered duress wipe that preserves innocuous step data while destroying sensitive health and surveillance data | Balances cover identity plausibility with data protection |
| 7 | Encrypt all health data at rest by default, not only in stealth mode | Health data is sensitive regardless of operational mode |

### Medium Priority

| # | Recommendation | Rationale |
|---|---------------|-----------|
| 8 | Make mood check-in reminder (`needs_checkin`) opt-in, not default | Forced self-assessment can increase distress |
| 9 | Add sleep quality estimation via accelerometer actigraphy | Low-burden, low-sensitivity, high evidentiary value |
| 10 | Add emergency contact quick-action (hardware button combo) | Direct safety function |
| 11 | Add simple breathing/grounding exercise | Immediate coping resource after receiving a surveillance alert |
| 12 | Implement "disclosure mode" for voluntary law enforcement interaction | Clean presentation of full device capabilities when user chooses |
| 13 | Automatic encrypted backup of health data to phone companion | Ensures duress wipe does not permanently destroy medical records |
| 14 | Mood data requires secondary PIN to view | Prevents stalker from reading mood scores if device is discovered |

### Low Priority

| # | Recommendation | Rationale |
|---|---------------|-----------|
| 15 | Consider moving sobriety counter to phone companion only | Reduces risk of discovery on the physical device |
| 16 | Health data exports in CSV with user-selected categories and optional timestamps | Practical interoperability for therapy/legal/medical use |
| 17 | Boot screen shows step count or date, not a branded splash | Unremarkable first impression if device is discovered |
| 18 | Health trend displays avoid value judgments ("Your mood over the past week" not "Your mood is declining") | Reduces evaluative pressure on the user |

### Do Not Implement

| Feature | Reason |
|---------|--------|
| Free-text journaling | Highest-risk data type; can be weaponized in any legal proceeding |
| Social/peer comparison features | Deanonymization risk, social pressure |
| Gamification / streaks / achievements | Creates guilt and pressure to engage |
| Auto-correlation of mood and surveillance events in stored data | Creates a pre-built legal vulnerability |
| Location tagging on health entries | Keeps surveillance data out of the health layer |

---

## 10. Final Assessment

The pivot from "surveillance detector disguised as wellness device" to "genuine wellness device with background surveillance detection" is the single most important design decision this project has made. It transforms the user's relationship to the device from one of chronic threat-monitoring to one of daily self-care with a protective layer underneath.

The health features are not a disguise. They are genuinely useful for this population. Mood tracking documents psychological impact. Step counting encourages physical activity during a period of likely depression. Cycle tracking provides health awareness. The sobriety counter supports recovery. These are real interventions for real problems that stalking victims face.

But the dual-purpose design creates new risks that the previous architecture did not have. Health data is a liability if it falls into the wrong hands. The same device that helps a victim document their suffering can provide a stalker with feedback on the effectiveness of their campaign. The same mood log that supports a prosecution can be used by a defense attorney to question the victim's baseline mental health.

The recommendations above attempt to navigate these tensions. The core principle is: **the device should be designed so that the worst-case scenario -- discovery by an abuser, seizure by law enforcement, examination by a hostile attorney -- causes the least possible harm to the user.** Health data protections, tiered duress wipes, and separated data streams all serve this principle.

The device cannot make a stalking victim safe. No technology can. But it can avoid making them less safe, and it can provide small daily anchors of normalcy in an abnormal situation. If the health features are implemented with the care and sensitivity described in this review, they represent a meaningful contribution to survivor safety and wellbeing.

---

## References

- Dimidjian, S., Hollon, S. D., Dobson, K. S., Schmaling, K. B., Kohlenberg, R. J., Addis, M. E., ... & Jacobson, N. S. (2006). Randomized trial of behavioral activation, cognitive therapy, and antidepressant medication in the acute treatment of adults with major depression. *Journal of Consulting and Clinical Psychology*, 74(4), 658-670.
- Kilpatrick, D. G., Acierno, R., Resnick, H. S., Saunders, B. E., & Best, C. L. (2003). A 2-year longitudinal analysis of the relationships between violent assault and substance use in women. *Journal of Consulting and Clinical Psychology*, 65(5), 834-847.
- Mehrabian, A., & Russell, J. A. (1974). *An Approach to Environmental Psychology*. MIT Press.
- Miller, E., Decker, M. R., McCauley, H. L., Tancredi, D. J., Levenson, R. R., Waldman, J., ... & Silverman, J. G. (2010). Pregnancy coercion, intimate partner violence and unintended pregnancy. *Contraception*, 81(4), 316-322.
- Mullen, P. E., Pathe, M., & Purcell, R. (2000). *Stalkers and Their Victims*. Cambridge University Press.
- Nagma, S., Kapoor, G., Bharti, R., Batra, A., Batra, A., Aggarwal, A., & Sablok, A. (2015). To evaluate the effect of perceived stress on menstrual function. *Journal of Clinical and Diagnostic Research*, 9(3), QC01-QC03.
