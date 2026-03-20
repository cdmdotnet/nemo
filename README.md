![build](https://github.com/cdmdotnet/nemo/actions/workflows/build.yml/badge.svg)

# Nemo-Plus
Nemo-Plus is a fork of [nemo](https://github.com/linuxmint/nemo) with additional dual-pane/preview-pane features similar to those of [xplorer²](https://www.zabkat.com/) on windows. This was done as nemo, being part of Linux Mint, has a strong reputation for being one of (if not the) best distros for those wishing to move from Windows to Linux. This has similarities to [4pane](https://4pane.co.uk/), which we would have stuck with, however found its layout/aesthetics a bit dated and out of place in Cinnamon along with cross app integration for features like copy-paste/drag-and-drop of files to be inconsistent when compared to nemo.

## History
Nemo-Plus started as a fork of nemo at v6.3.3 and we intend to keep it up-to-date with upstream features as best we can along with matching version numbers as best we can.

## Features
### Dual-pane enhancements:
Adds the ability to switch between the existing horizontal only dual-pane split to a vertical dual-pane split

- **Show panes vertically** (one above the other instead of side by side)
- **Separate sidebars per per pane** (each tracking its own location independently in the folder tree) - which is more visually fitting with a horizontal dual-pane split vs a vertical split. This means changing folders in a pane doesn't first require you to manually switch panes so the side bar updates, and then use it. While this sounds like removing just one click, it's terribly frustrating searching in your folders, finding the folder you wanted, clicking it and having the wrong pane change folders.
- **Separate navigation bars per per pane** (referred to as Path Bar/Location Entry in nemo) - mostly because it makes it much, much easier to track which folder you are in for each pane, especially when working with folders with similar content, or copy the location easily from the Location Entry in the same way you would from a browsers address bar, again without first having to change which pane.
- **Separate statusbars per per pane** (each pane shows its own file count, selection info, and zoom control) - since they provide valuable and useful information, again it's about making this visible at a glance without again having to do that each click... or worse thinking you are in the right pane and having the wrong information visible and display for where your mind/brain is at... your context

All of these extra features are optional can be toggled on or off. So if you like some feature and not other you don't have to have them all. You can even turn them all off and go back to stock nemo without uninstalling it, useful for wcollaborating or having a conversation with someone who is more comfortable with their layout choices, and turn it back on afterwards easily.

### Preview-pane enhancements
Adds a Preview Pane that shows a live preview of selected file in a resizable right-hand panel.

Supported file types:
- **Images** — scaled to fit while preserving aspect ratio and without upscaling/overscaling
- **Audio** — plays inline with play/pause and seek controls
- **Video** — plays inline with play/pause and seek controls
- **PDF** — scrollable page view
- **HTML** — rendered in an embedded web view
- **Office documents** (Writer, Calc, Impress, Word, Excel, PowerPoint)
  converted via LibreOffice and rendered inline

When a supported file type is selected but the required runtime library is not installed, the pane shows a friendly notice explaining what to install, with a selectable install command and a Copy to Clipboard button - for those that don't like/trust using "copy to clipboard" buttons. Unsupported file types show a plain empty pane.

Previewing works on both local files and network shares (SMB, SFTP, etc.) — files are transparently copied to a temporary location as
needed.