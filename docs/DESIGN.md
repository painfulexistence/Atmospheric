# Atmospheric — Design System

Design tokens for the project website and documentation. Derived from the existing devlog HTML palette.

---

## Color System

| Token | Hex | Usage |
|-------|-----|-------|
| `--color-bg` | `#0f1117` | Page background |
| `--color-surface` | `#181c27` | Cards, panels |
| `--color-surface-2` | `#1e2333` | Elevated surfaces, code blocks bg |
| `--color-border` | `#2a3045` | Dividers, borders |
| `--color-primary` | `#7c9ef7` | Links, active states, primary CTA |
| `--color-accent` | `#e07b54` | Highlights, warnings, secondary CTA |
| `--color-accent-2` | `#6ecfa4` | Success states, tags |
| `--color-text` | `#d4daf0` | Body text |
| `--color-muted` | `#7a84a0` | Captions, metadata, placeholders |
| `--color-danger` | `#e05c5c` | Errors |
| `--color-warn` | `#d4a84b` | Warnings |
| `--color-code-bg` | `#12151f` | Inline code, code block background |

### Palette Rationale
Colors taken directly from the existing `webgpu_support_devlog_1.html` and `2d_coordinate_and_texture_alignment_devlog.html`. The deep navy backgrounds (`#0f1117`) evoke a night-sky / atmospheric theme; the blue-indigo primary (`#7c9ef7`) and warm orange accent (`#e07b54`) create contrast without harshness.

---

## Typography

### Fonts (Google Fonts)

| Role | Family | Weight | Usage |
|------|--------|--------|-------|
| Heading | [Inter](https://fonts.google.com/specimen/Inter) | 600, 700 | `h1`–`h3`, nav, CTAs |
| Body | [Inter](https://fonts.google.com/specimen/Inter) | 400 | Paragraphs, lists |
| Monospace | [JetBrains Mono](https://fonts.google.com/specimen/JetBrains+Mono) | 400 | Code blocks, inline code, technical values |

### Import Snippet
```html
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&family=JetBrains+Mono:wght@400&display=swap" rel="stylesheet">
```

### Scale
```css
--font-heading: 'Inter', system-ui, sans-serif;
--font-body:    'Inter', system-ui, sans-serif;
--font-mono:    'JetBrains Mono', 'Fira Code', monospace;

--text-xs:   0.75rem;   /* captions */
--text-sm:   0.875rem;  /* metadata, labels */
--text-base: 1rem;      /* body */
--text-lg:   1.125rem;  /* lead text */
--text-xl:   1.25rem;   /* h3 */
--text-2xl:  1.5rem;    /* h2 */
--text-3xl:  2rem;      /* h1 */
--text-4xl:  2.75rem;   /* hero */
```

---

## Spacing & Layout

```css
--spacing-xs:  4px;
--spacing-sm:  8px;
--spacing-md:  16px;
--spacing-lg:  24px;
--spacing-xl:  40px;
--spacing-2xl: 64px;

--max-width: 860px;
--border-radius: 6px;
--border-radius-sm: 3px;
```

---

## Component Tokens

```css
/* Inline code */
--code-padding: 2px 6px;
--code-border-radius: var(--border-radius-sm);

/* Cards / panels */
--card-padding: 20px 24px;
--card-border: 1px solid var(--color-border);

/* Focus ring */
--focus-ring: 0 0 0 2px var(--color-primary);

/* Modal */
--modal-overlay-bg: rgba(0, 0, 0, 0.85);
--modal-max-width: 1024px;
--modal-max-height: 640px;
--modal-shadow: 0 20px 50px rgba(0, 0, 0, 0.8);
```

<!-- spec:yaml
tokens:
  --color-bg: "#0f1117"
  --color-surface: "#181c27"
  --color-surface-2: "#1e2333"
  --color-border: "#2a3045"
  --color-primary: "#7c9ef7"
  --color-accent: "#e07b54"
  --color-accent-2: "#6ecfa4"
  --color-text: "#d4daf0"
  --color-muted: "#7a84a0"
  --color-danger: "#e05c5c"
  --color-warn: "#d4a84b"
  --color-code-bg: "#12151f"

nav:
  logo-class: "nav-logo"
  logo-text: "Atmospheric"
  links-class: "nav-inner"
  links:
    - "Docs"
    - "Devlog"
    - "Demo"
    - "GitHub ↗"

typography:
  body-size: "1rem"
  body-line-height: "1.75"
  font-family: "'Inter', system-ui, sans-serif"
  h1-size: "2rem"
  h1-weight: "700"
  h2-size: "1.5rem"
  h2-weight: "700"

layout:
  max-width: "860px"

modal:
  --modal-overlay-bg: "rgba(0, 0, 0, 0.85)"
  --modal-max-width: "1024px"
  --modal-max-height: "640px"
  --modal-shadow: "0 20px 50px rgba(0, 0, 0, 0.8)"

css-output: "styles/global.css"
-->
