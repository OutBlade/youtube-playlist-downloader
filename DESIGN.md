---
name: BLADE Playlist Downloader
description: A restrained charcoal workspace with precise pale type and thin horizontal rules.
colors:
  background: "#111214"
  surface: "#1b1d20"
  ink: "#f2f3f4"
  secondary: "#afb3b9"
  muted: "#9298a1"
  rule: "#34383e"
  field-border: "#454a52"
  selected-border: "#626871"
  selected-surface: "#24272c"
  button-hover: "#d6dae0"
  button-active: "#c4c9d1"
  disabled-background: "#777d86"
  disabled-ink: "#16181b"
  error: "#ffb9b0"
typography:
  display:
    fontFamily: "Manrope, system-ui, sans-serif"
    fontSize: "clamp(40px, 5.5vw, 66px)"
    fontWeight: 600
    lineHeight: 1.13
    letterSpacing: "-.035em"
  title:
    fontFamily: "Manrope, system-ui, sans-serif"
    fontSize: "16px"
    fontWeight: 600
    letterSpacing: "-.015em"
  body:
    fontFamily: "Manrope, system-ui, sans-serif"
    fontSize: "15px"
    fontWeight: 400
    lineHeight: 1.6
  label:
    fontFamily: "Manrope, system-ui, sans-serif"
    fontSize: "13px"
    fontWeight: 400
  action:
    fontFamily: "Manrope, system-ui, sans-serif"
    fontSize: "14px"
    fontWeight: 600
  detail:
    fontFamily: "Manrope, system-ui, sans-serif"
    fontSize: "12px"
    fontWeight: 400
  log:
    fontFamily: "ui-monospace, monospace"
    fontSize: "12px"
    lineHeight: 1.6
rounded:
  control: "5px"
spacing:
  icon-gap: "8px"
  control-gap: "12px"
  row-gap: "16px"
  field-inset: "20px"
components:
  button-primary:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.background}"
    typography: "{typography.action}"
    rounded: "{rounded.control}"
    padding: "0 25px"
  button-primary-hover:
    backgroundColor: "{colors.button-hover}"
  button-primary-active:
    backgroundColor: "{colors.button-active}"
  button-primary-disabled:
    backgroundColor: "{colors.disabled-background}"
    textColor: "{colors.disabled-ink}"
  url-field:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "0 20px"
    height: "62px"
  format-option:
    textColor: "{colors.secondary}"
    typography: "{typography.label}"
    rounded: "{rounded.control}"
    padding: "9px 14px"
  format-option-selected:
    backgroundColor: "{colors.selected-surface}"
    textColor: "{colors.ink}"
  source-link:
    textColor: "{colors.secondary}"
    typography: "{typography.label}"
  progress-line:
    backgroundColor: "{colors.rule}"
    height: "3px"
    width: "100%"
---

# Design System: BLADE Playlist Downloader

## Overview

**Creative North Star: "The Independent Record-label Sleeve"**

Charcoal, a white wordmark, precise pale lettering, and quiet graphite rules establish the visual character. The actual BLADE GIF remains the identity authority; its cropped horizontal silhouette sets the tone. Imagery comes from the content itself: the playlist's own thumbnails.

The system is spacious around major transitions and compact within controls. Its character is restrained and practical: broad rectangular actions, softly eased corners, straightforward language, and a fine horizontal progress cut. The visual hierarchy comes from size, tone, and spacing.

**Key Characteristics:**

- Charcoal surfaces and pale monochrome controls.
- Self-hosted Manrope at regular and semibold weights.
- Thin rules and a slim progress line.
- Broad rectangular controls with gently eased corners.
- Clear focus and state changes.

## Colors

Pale neutrals sit on charcoal with graphite separators; a warm error tone is reserved for invalid input and errors.

### Primary

- **Pale Ink:** The primary action, prominent text, progress fill, and focus outlines use the ink token.

### Neutral

- **Charcoal:** The background anchors the full workspace and supplies dark action text.
- **Graphite Surface:** The field and expandable log use the surface token.
- **Soft Silver:** Secondary text supports descriptions, labels, and navigation.
- **Slate:** Muted text identifies metadata, placeholders, and quiet status.
- **Graphite Rule:** The rule token separates major regions and file rows and forms the progress track.
- **Field Graphite:** Field-border and selected-border provide distinct input and checked-option boundaries.
- **Selected Charcoal:** Selected-surface marks the checked format.
- **Action Silver:** Button-hover and button-active provide instantaneous pointer feedback.
- **Disabled Slate:** Disabled-background and disabled-ink indicate unavailable actions.

The error token is a functional warm exception to the neutral palette, not a promotional accent.

**The Monochrome Authority Rule.** Preserve the neutral visual hierarchy; reserve the warm error tone for error states. The only other colour on the page belongs to thumbnails of saved items.

## Typography

**Display Font:** Manrope with system-ui and sans-serif fallbacks.

**Body Font:** Manrope with the same fallbacks. Regular and semibold TTF files are self-hosted.

**Label/Mono Font:** UI monospace appears only in the expandable technical log.

Manrope gives both display text and practical labels a precise, open character. Semibold marks headings and actions; regular carries explanation and metadata.

### Hierarchy

- **Display:** The display token supplies the large heading, with balanced wrapping. On narrow screens it becomes (43px).
- **Title:** The title token supplies section headings. Job titles step down to (15px), retaining the same semibold weight and tight tracking.
- **Body:** The body token supplies the default reading rhythm. Introductory text uses (16px), constrained to (68ch); narrow screens use (14px) with line height (1.8).
- **Label:** The label token supplies form labels, source navigation, format controls, messages, and file rows.
- **Action:** The action token supplies primary and archive actions.
- **Detail:** The detail token supplies status, limits, footer, and file metadata. Numeric progress and sizes use tabular figures. The narrow state label steps down to (11px).
- **Log:** The log token supplies wrapped technical detail inside an overflow-limited surface.

## Layout

The centered page has a maximum width of (1088px), horizontal padding of (52px), and a minimum height of (100svh). The header is (126px) tall; the main area begins with (82px) of top space. The footer uses automatic top margin to settle below short content.

The input and action share a flex row with the control-gap token. Format choices and a quiet limit note form a second row. A top rule and (54px) of separation introduce the download region; file rows use thin bottom separators. Container regions remain open on the page background.

At the implemented breakpoint (640px), page gutters become (24px), the header becomes (94px), and main top space becomes (53px). Input and action stack; the field is (58px) tall and the primary action has a minimum height of (54px). Option metadata, job summary, and footer also stack. The archive action fills the available width. There is no separate tablet layout.

## Elevation & Depth

No shadows are used. Depth comes from the graphite field and log surfaces against the charcoal page, plus thin boundaries and strong ink contrast. No hover lift or animated transforms are applied to ordinary controls.

**The Flat Surface Rule.** Use tonal surfaces and fine boundaries to distinguish controls; retain the flat appearance of the built system.

## Shapes

Controls and the technical log share the control radius: softly eased corners on rectangular forms. Structural regions use straight horizontal rules with (1px) borders. The progress track remains a fine, square-ended cut. Actual controls have eased corners even though the directional brief described square edges.

The wordmark is the preserved source GIF, displayed through a horizontal crop with screen blending. Keep its supplied artwork and proportions; the small-screen brand is drawn at (0.82) of its desktop size.

## Components

### Buttons

Broad, clear actions use ink over charcoal text and the shared control radius. The desktop download action has a minimum height of (62px); archive delivery uses (48px) before the shared mobile action rule applies. Hover, active, and disabled variants use their frontmatter colors. Focus uses an ink outline (2px) with an offset of (5px). State feedback is immediate.

### Inputs / Fields

The URL field uses the graphite surface, field-border, a stroke icon, and a clear text input. Focus within changes its border to ink. The placeholder uses muted text; invalid input uses the error token. Text remains inset, with the icon separated by (14px). The visible label is part of the field pattern.

### Format choices

Radio choices use compact rectangular labels with transparent default boundaries. The checked option gains selected-surface, selected-border, and ink text. Hover changes the text to ink. Keyboard focus outlines the visible label with ink (2px), offset (3px). This is a native radio choice, visually styled as a small format control.

### Navigation

Source and footer links pair quiet text with small stroke SVG arrows. Hover changes the text to ink; keyboard focus uses the shared outline. The source link remains visible on mobile and steps down to (12px). The footer stacks vertically on narrow screens.

### Sleeve and record

Beside the heading on wide screens (hidden below 960px) sits a square black sleeve carrying the unaltered BLADE artwork, with a grooved record behind it. During a download the sleeve shows the playlist's first four thumbnails (one for a single video) and the record slides out in step with overall progress while its label turns. It is decorative and hidden from assistive technology; the download region carries the same state as text.

### Contact sheet

Playlist items appear as a grid of 16:9 thumbnails (minimum 210px columns, two columns on narrow screens) with a two-digit position, duration or status, and a two-line title. Colour is the state signal: queued frames are dim greyscale, the active frame is bright greyscale with the progress line along its lower edge, saved frames develop into full colour (0.9s, exponential ease-out), and unavailable frames stay dark with their reason in place of the duration. A saved frame is a link to its file and shows a small ink save mark. The empty state is the same sheet with four blank outlined frames.

### Download progress and file delivery

The slim progress-line token separates the job heading from its message. Determinate progress uses ink fill; indeterminate progress scans a short ink segment through the graphite track (2s, linear, infinite). The native progress element carries the accessible progress semantics. Reduced-motion preference disables animation.

Completed files use open rows with a bottom rule, wrapping filenames, and muted tabular sizes. File links underline on hover. The archive action inherits the primary button. The technical log is disclosed through a native details/summary element, with wrapped monospace text and a maximum scroll height of (200px).

## Do's and Don'ts

### Do:

- **Do** preserve the actual BLADE GIF artwork.
- **Do** use Manrope regular and semibold for the established hierarchy.
- **Do** keep controls visibly focused and format selection unambiguous.
- **Do** use thin graphite rules and the slim progress line for structural separation.
- **Do** preserve the stacked narrow-screen form and wrapping filenames.

### Don't:

- **Don't** replace the supplied wordmark with a text approximation.
- **Don't** add neon accents or decorative gradients to the visual world.
- **Don't** add shadows to the flat control system.
- **Don't** treat the error color as a decorative accent.
