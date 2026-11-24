# Amplia Help

Amplia is a new player for the Linux desktop with the soul of Winamp: one window, no
browser, and the music keeps playing while you fiddle with the list.

## The main window

- **Display**: elapsed time (or remaining, with `Ctrl+T`), track title, artist, and on the
  right the bitrate, sample rate and channel indicators. `kHz` and `STEREO` light up when
  the file has something to show.
- **Buttons**: previous, play, pause, stop, next, eject and volume. `L R` is the level
  meter; `EQ` and `123` light up the equalizer and the track counter.
- **Scrolling title**: the title bar shows the track and artist, scrolling when it does not
  fit.
- **Footer**: on the left the metadata queue counter and the version; on the right the
  transient messages.

## The playlist

One row per track, with duration; the playing track is marked and, when the queue panel is
open, the queue sits right under it.

- **Sort**: title, artist, album, path, duration or modification date. `Shuffle` shuffles
  the order; `Repeat` makes the list loop.
- **Enqueue** (or `Alt+click` a row) puts the track ahead of the order without losing the
  rest. Queue entries carry an origin: playlist, library or radio.
- **Remove**: `Delete` removes the selection, `Ctrl+Delete` keeps only it, `Alt+Delete`
  drops files that vanished from disk.
- **Editing**: drag rows to reorder, `Ctrl+arrows` move the selection around.
- **Font**: `Ctrl+=` and `Ctrl+-` grow and shrink the text; `Ctrl+Shift+0` resets it.

## The queue (N)

The `Fila (N) ⌄` button opens and closes the panel. Each row says where the track came from
(`PLAYLIST`, `BIBLIOTECA`, `RÁDIO`) and how long it is. The queue **survives closing**: the
app stores the list and the origins, and on the next start it comes back — paused, on the
same track, so you decide when to continue.

Rejecting a radio track (removing it from the queue) is information: it feeds the history
the station uses to avoid what you did not want to hear.

## The Library

`Opções ▸ Biblioteca` opens a window with the whole collection, separate from what is
playing.

- **Views**: all tracks, added in the last 3 days, modified in the last 3 days, played in
  the last 2 weeks, never played, most played and top rated.
- **Search**: filters by title, artist or album as you type.
- **Columns**: click a header to sort; `Nota` takes a click to rate 0 to 5 stars, and
  clicking the same rating clears it.
- **Footer**: Play, Enqueue and (in radio mode) Play station. `Alt+click` a row enqueues
  instead of playing; double click plays.

### Humor (mood)

The **Humor** node is a two-axis plane: **Agitation** (slow ↔ fast, driven by BPM) and
**Tone** (cold, minor ↔ warm, major). **Stability** sets the width of the target — from
"this track and its neighbours" to "the whole station may drift three BPM steps". The fourth
slider is the **Blend**: 0% is only the mood, 100% is only what you have been listening to.

- The mood list (factory and yours) lives in the **combo** next to the title. Picking one
  loads the four sliders.
- Moving the sliders is a **draft**: the preview below changes right away, but the queue
  only starts on **Play**. The orange notice says so, and **Revert** puts the sliders back
  on the chosen mood.
- **Save as…** stores the point under a name of yours; **Remove** deletes yours. Factory
  moods do not change: save a variation.
- Until the library has been measured, the preview is decided by the mood's **genre
  lexicon**. As you listen, measured BPM and key start to weigh in — and the station gets
  better. The footer shows how many tracks have been measured.

### Local Radio

**Local Radio** is the irradiation of the playing track: the station starts from it and
from what you have been listening to (the last five), with a slider deciding how much of
each.

- The first queue entry is the seed; the rest is the station.
- Picking rules: artist, album, genre, era, tempo and key; the same track is never
  suggested twice, repeated artists are spaced out in the first picks, and now and then a
  discovery from someone you barely play comes in.
- The panel shows the preview: 20 tracks the station would use right now.

## Alt+3 — file information

Shows and edits tags (title, artist, album, track, year, genre, comment, composer,
publisher) and BPM (`TBPM`). On the right, two readings:

- **Local analysis**: `BPM da tag` and `Tom da tag` come from the file itself (`TBPM` and
  `TKEY`); `BPM percebido` and `Tom percebido` are measured while playing and are **not**
  written to the file.
- Writing is safe by construction: the app writes a temporary copy and swaps it in one go,
  and warns that interrupting the process halfway may corrupt the tags.

## Equalizer, appearance and preferences

- `Alt+G` opens the equalizer with ready-made curves and your saved curve.
- `Ctrl+,` opens Appearance: ready themes, the colour editor and the playlist font.
- `Ctrl+P` opens Preferences: output device, ReplayGain, Last.fm, library folders,
  language.

## Shortcuts

| Key | What it does |
| --- | --- |
| `X` / `Space` / `C` | Play / pause / pause |
| `B` / `Z` | Next / previous |
| `V` | Stop |
| `S` / `R` | Shuffle / repeat |
| `→` / `←` | Forward and back 5 seconds |
| `↑` / `↓` | Volume |
| `M` | Mute |
| `Num 1` / `Num 3`, `Ctrl+←` / `Ctrl+→` | Jump 10 tracks |
| `L` / `Shift+L` | Add files / add folder |
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | New playlist / open / save |
| `Delete` / `Ctrl+Delete` / `Ctrl+Shift+Delete` | Remove / keep only the selection / clear |
| `Alt+Delete` | Remove the files that vanished |
| `,` (comma) | Jump to file… |
| `Alt+3` | File information |
| `Ctrl+Shift+Q` | Open and close the queue |
| `Ctrl+Shift+R` | Shuffle the list (playlist) |
| `Ctrl+Alt+A` / `Ctrl+Alt+D` / `Ctrl+I` | Select all / none / invert |
| `Ctrl+T` | Toggle elapsed and remaining |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+Shift+0` | Playlist font: bigger / smaller / default |
| `Alt+W` / `Alt+E` / `Alt+G` | Main window / playlist / equalizer |
| `Ctrl+Tab` | Cycle through open windows |
| `Ctrl+P` / `Ctrl+,` | Preferences / appearance |
| `F1` | This help |

## Where Amplia keeps its files

Everything lives inside the Flatpak sandbox, under `~/.var/app/io.github.cassiofiorin.Amplia/`:

- `config/amplia/amplia.ini` — preferences, window, playlist and queue;
- `data/cassiofiorin/Amplia/library.db` — library, ratings, radio history and saved moods;
- `data/cassiofiorin/Amplia/` — caches (artwork, analysis).

## Sending feedback

`Ajuda ▸ Enviar opinião` opens the project issues:
<https://github.com/cassiofiorin/Amplia/issues>. A good report says what you did, what you
expected and what happened — and, for sound issues, the file format helps.

## Translating Amplia

The help for each language is a text file in this folder (`ajuda-pt.md`, `ajuda-en.md`), and
the translator list sits in `tradutores.txt` — it shows up in `Sobre o Amplia…`. Translators
only touch their own file and one line of text: no code.
