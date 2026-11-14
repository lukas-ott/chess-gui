// Kiebitz website: draws the boards and fills in the newest release from GitHub.

const REPO = "lukas-ott/kiebitz";

// Boards: <div class="board" data-fen="…" data-arrow="e2e4">
function drawBoard(el) {
    const rows = el.dataset.fen.split(" ")[0].split("/");
    const files = "abcdefgh";
    const arrow = el.dataset.arrow || "";
    const frag = document.createDocumentFragment();
    rows.forEach((row, r) => {
        let f = 0;
        for (const ch of row) {
            if (/\d/.test(ch)) {
                for (let i = 0; i < Number(ch); i++) frag.append(square(r, f++, null));
            } else {
                frag.append(square(r, f++, ch));
            }
        }
    });
    el.replaceChildren(frag);
    if (arrow.length === 4) el.append(arrowSvg(arrow));

    function square(r, f, piece) {
        const sq = document.createElement("div");
        const name = files[f] + (8 - r);
        sq.className = "sq" + ((r + f) % 2 ? " dark" : "") + (name === arrow.slice(0, 2) ? " from" : "");
        if (piece) {
            const img = document.createElement("img");
            const colour = piece === piece.toUpperCase() ? "w" : "b";
            img.src = `assets/pieces/${colour}${piece.toUpperCase()}.svg`;
            img.alt = "";
            sq.append(img);
        }
        if (r === 7 && !el.classList.contains("board-small")) sq.append(coord("file", files[f]));
        if (f === 0 && !el.classList.contains("board-small")) sq.append(coord("rank", 8 - r));
        return sq;
    }
    function coord(kind, text) {
        const s = document.createElement("span");
        s.className = "coord " + kind;
        s.textContent = text;
        return s;
    }
}

function arrowSvg(move) {
    const at = (sq) => [sq.charCodeAt(0) - 97 + 0.5, 8 - Number(sq[1]) + 0.5];
    const [x1, y1] = at(move.slice(0, 2));
    const [x2, y2] = at(move.slice(2, 4));
    const len = Math.hypot(x2 - x1, y2 - y1);
    const ux = (x2 - x1) / len;
    const uy = (y2 - y1) / len;
    const head = 0.42;
    const ex = x2 - ux * head;
    const ey = y2 - uy * head;
    const px = -uy * 0.26;
    const py = ux * 0.26;
    const ns = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(ns, "svg");
    svg.setAttribute("viewBox", "0 0 8 8");
    svg.setAttribute("class", "arrow");
    svg.setAttribute("aria-hidden", "true");
    svg.innerHTML =
        `<line x1="${x1 + ux * 0.2}" y1="${y1 + uy * 0.2}" x2="${ex}" y2="${ey}" stroke="#b5432f" stroke-opacity="0.85" stroke-width="0.17" stroke-linecap="round"/>` +
        `<polygon points="${x2},${y2} ${ex + px},${ey + py} ${ex - px},${ey - py}" fill="#b5432f" fill-opacity="0.85"/>`;
    return svg;
}

// A made-up but steady training grid (same pattern on every visit).
function drawActivity(el) {
    let seed = 7;
    const rand = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
    const days = 7 * 22;
    for (let i = 0; i < days; i++) {
        const cell = document.createElement("i");
        const recent = i > days - 10;
        const x = rand();
        cell.dataset.l = recent ? 2 + Math.round(rand()) : x < 0.35 ? 0 : x < 0.6 ? 1 : x < 0.85 ? 2 : 3;
        el.append(cell);
    }
}

// Newest release (pre-releases included): version, date and the .dmg link.
async function loadRelease() {
    try {
        const res = await fetch(`https://api.github.com/repos/${REPO}/releases?per_page=5`, {
            headers: { Accept: "application/vnd.github+json" },
        });
        if (!res.ok) return;
        const release = (await res.json()).find((r) => !r.draft);
        const dmg = release && release.assets.find((a) => a.name.endsWith(".dmg"));
        if (!dmg) return;
        const version = release.tag_name.replace(/^v/, "");
        const mb = Math.round(dmg.size / 1e6);
        const date = new Date(release.published_at).toLocaleDateString("en-GB", {
            day: "numeric",
            month: "long",
            year: "numeric",
        });
        document.querySelectorAll("[data-download]").forEach((a) => (a.href = dmg.browser_download_url));
        document.querySelectorAll("[data-version]").forEach((s) => (s.textContent = `Version ${version}`));
        document.querySelectorAll("[data-download-label]").forEach(
            (s) => (s.textContent = `Download Kiebitz ${version} (.dmg, ${mb} MB)`));
        document.querySelectorAll("[data-released]").forEach(
            (s) => (s.textContent = `Version ${version}, released ${date}.`));
    } catch {
        // Offline or rate-limited: the links in the page already point at a release.
    }
}

document.querySelectorAll(".board[data-fen]").forEach(drawBoard);
document.querySelectorAll(".activity").forEach(drawActivity);
loadRelease();
