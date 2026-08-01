// app.js — interface de l'editeur.
//
// Meme mecanique que style.css : charge par <script src> dans le
// navigateur, et insere dans la page par main_webview.cpp pour
// l'application. Relu a chaque lancement, aucune recompilation.
//
// Ce fichier contient DEUX parties, dans cet ordre imperatif :
//   1. le backend simule (dev-mock), qui doit exister AVANT le script
//      principal ; il n'est actif que HORS de l'application ;
//   2. l'interface elle-meme.

// =====================================================================
// 1. BACKEND SIMULE (dev-mock)
//
// Defini AVANT le script principal pour etre disponible au demarrage.
// Actif SEULEMENT hors webview : il rejoue en memoire les commandes du
// pont pour tester l'interface dans un navigateur. Approximatif (undo non
// groupe par mot) : le vrai moteur C++, teste unitairement, fait reference.
// =====================================================================
if (typeof window.cppInvoke !== "function") {
  (function () {
    const SEPS = new Set([" ","\t","\n","\r",".",",",";",":","!","?","(",")","[","]","{","}",'"',"«","»"]);
    const isSep = (c) => SEPS.has(c);

    // Modèle : blocs de { type, cells:[{c, s:Set}] } + métadonnées.
    let blocks = [{ type: "p", cells: [] }];
    let meta = { title: "", author: "", cover: "", summary: "" };
    const undoStack = [], redoStack = [];

    // --- Fiches (miroir de core::builtinSchemas + CardLibrary) --------
    const F = (key, label, type) => ({ key, label, type });
    const cardSchemas = [
      { typeId: "creature", label: "Créature", fields: [
        F("description_physique","Description physique","longtext"), F("age","Âge","text"),
        F("caractere","Caractère","longtext"), F("relations","Relations","cardrefs"),
        F("historique","Historique","longtext"), F("deces_chapitre","Décès (chapitre)","chapterref"),
        F("illustrations","Illustrations","images")] },
      { typeId: "langue", label: "Langue", fields: [
        F("inspiration","Inspiration (latin, grec...)","text"), F("faction","Faction / royaume","text"),
        F("equivalences","Équivalences de mots","list")] },
      { typeId: "lieu", label: "Lieu", fields: [
        F("ambiance","Ambiance","longtext"), F("localisation","Localisation","text"),
        F("faction","Pays / faction","text"), F("points_interet","Points d'intérêt","list"),
        F("historique","Informations historiques","longtext"), F("sous_lieux","Sous-lieux","cardrefs"),
        F("illustrations","Illustrations","images")] },
      { typeId: "magie", label: "Magie", fields: [
        F("element","Élément","text"), F("niveau","Niveau","text"),
        F("effet","Effet","longtext"), F("description","Description","longtext"),
        F("illustrations","Illustrations","images")] },
      { typeId: "personnage", label: "Personnage", fields: [
        F("description_physique","Description physique","longtext"), F("age","Âge","text"),
        F("caractere","Caractère","longtext"), F("relations","Relations","cardrefs"),
        F("historique","Historique","longtext"), F("deces_chapitre","Décès (chapitre)","chapterref"),
        F("illustrations","Illustrations","images")] },
      { typeId: "religion", label: "Religion", fields: [
        F("valeurs","Valeurs","longtext"), F("dieux","Dieux","list"),
        F("dirigeants","Dirigeants","list"), F("membres","Personnages membres","cardrefs")] },
    ];
    const cardsById = new Map();
    let cardsRoot = "";

    const foldAccents = (t) => {
      const map = {"à":"a","â":"a","ä":"a","á":"a","ã":"a","å":"a","ç":"c","é":"e","è":"e","ê":"e",
                   "ë":"e","î":"i","ï":"i","í":"i","ì":"i","ô":"o","ö":"o","ó":"o","ò":"o","õ":"o",
                   "ù":"u","û":"u","ü":"u","ú":"u","ÿ":"y","ñ":"n"};
      let s = ""; for (const c of t.toLowerCase()) s += (map[c] !== undefined ? map[c] : c);
      return s;
    };
    const slugifyJs = (t) => {
      const s = foldAccents(t || "").replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
      return s || "fiche";
    };
    const sortByTitle = (a, b) => foldAccents(a.title || "").localeCompare(foldAccents(b.title || ""));
    const summary = (c) => {
      const s = { id: c.id, type: c.type, title: c.title, tags: c.tags.slice() };
      const d = c.fields["deces_chapitre"];
      if (d && d.length) s.deathChapter = d[0];
      return s;
    };
    const cardCopy = (c) => ({ id: c.id, type: c.type, title: c.title,
                               tags: c.tags.slice(), fields: JSON.parse(JSON.stringify(c.fields)) });

    const cps = (str) => Array.from(str);
    function deepCopy() {
      return blocks.map((b) => ({ type: b.type, cells: b.cells.map((x) => ({ c: x.c, s: new Set(x.s), sz: x.sz || 100 })) }));
    }
    function snapshot() { undoStack.push(deepCopy()); redoStack.length = 0; if (undoStack.length > 200) undoStack.shift(); }

    function serialize(b) {
      const text = b.cells.map((x) => x.c).join("");
      const runs = [];
      let i = 0;
      while (i < b.cells.length) {
        const s = b.cells[i].s;
        const sz = b.cells[i].sz || 100;
        let j = i + 1;
        while (j < b.cells.length && sameSet(b.cells[j].s, s) && (b.cells[j].sz || 100) === sz) j++;
        if (s.size || sz !== 100) runs.push({ start: i, length: j - i, styles: [...s], size: sz });
        i = j;
      }
      return { type: b.type, text, runs };
    }
    function sameSet(a, b) { if (a.size !== b.size) return false; for (const x of a) if (!b.has(x)) return false; return true; }
    function allBlocks() { return blocks.map(serialize); }

    // État de compte simulé, en mémoire seulement (Phase 5, jalon C).
    const mockAccount = {
      connected: false, server: "", user: "",
      pairing: { state: "sans-fichier", mode: "lecture-seule", autoSync: false,
                 server: "", canPropose: false },
    };

    function wordCount() {
      let n = 0;
      blocks.forEach((b) => { let inW = false; b.cells.forEach((x) => { if (isSep(x.c)) inW = false; else if (!inW) { inW = true; n++; } }); });
      return n;
    }

    // --- Recherche (miroir de core::Search) ---
    function foldLower(ch) {
      const c = ch.codePointAt(0);
      if (c >= 65 && c <= 90) return String.fromCodePoint(c + 32);
      if (c >= 0xC0 && c <= 0xDE && c !== 0xD7) return String.fromCodePoint(c + 32);
      return ch;
    }
    function chEq(a, b, cs) { return a === b || (!cs && foldLower(a) === foldLower(b)); }
    function matchAt(cells, pos, q, opts) {
      if (pos + q.length > cells.length) return false;
      for (let k = 0; k < q.length; k++) if (!chEq(cells[pos + k].c, q[k], opts.caseSensitive)) return false;
      if (opts.wholeWord) {
        const okBefore = pos === 0 || isSep(cells[pos - 1].c);
        const after = pos + q.length;
        const okAfter = after === cells.length || isSep(cells[after].c);
        if (!okBefore || !okAfter) return false;
      }
      return true;
    }
    function matchesInCells(cells, q, opts) {
      const out = [];
      if (!q.length || q.length > cells.length) return out;
      let i = 0;
      while (i + q.length <= cells.length) {
        if (matchAt(cells, i, q, opts)) { out.push(i); i += q.length; } else i++;
      }
      return out;
    }
    function scopeOf(p) {
      const n = blocks.length;
      const rs = p.rangeStart || 0;
      const re = (p.rangeEnd == null) ? n : Math.min(p.rangeEnd, n);
      return [rs, re];
    }

    const handlers = {
      "editor.getDocument": () => ({ blocks: allBlocks() }),
      "editor.find": (p) => {
        const q = cps(p.query || "");
        const opts = { caseSensitive: !!p.caseSensitive, wholeWord: !!p.wholeWord };
        const [rs, re] = scopeOf(p);
        const all = [];
        for (let b = rs; b < re; b++) {
          for (const pos of matchesInCells(blocks[b].cells, q, opts)) all.push({ block: b, start: pos });
        }
        const res = { found: false, block: 0, start: 0, end: 0, index: 0, total: all.length };
        if (!q.length || !all.length) return res;
        const fb = p.fromBlock || 0, fp = p.fromPos || 0;
        const wrap = (p.wrap == null) ? true : !!p.wrap;
        let chosen = -1;
        if (!p.backward) {
          for (let i = 0; i < all.length; i++) {
            if (all[i].block > fb || (all[i].block === fb && all[i].start >= fp)) { chosen = i; break; }
          }
          if (chosen < 0 && wrap) chosen = 0;
        } else {
          for (let i = all.length - 1; i >= 0; i--) {
            if (all[i].block < fb || (all[i].block === fb && all[i].start < fp)) { chosen = i; break; }
          }
          if (chosen < 0 && wrap) chosen = all.length - 1;
        }
        if (chosen < 0) return res;
        res.found = true;
        res.block = all[chosen].block;
        res.start = all[chosen].start;
        res.end = all[chosen].start + q.length;
        res.index = chosen + 1;
        return res;
      },
      "editor.replaceRange": (p) => {
        snapshot();
        const rep = cps(p.text || "").map((c) => ({ c, s: new Set() }));
        blocks[p.block].cells.splice(p.start, p.count, ...rep);
        return { block: serialize(blocks[p.block]), blockIndex: p.block, caretPos: p.start + rep.length };
      },
      "editor.replaceAll": (p) => {
        const q = cps(p.query || "");
        const repCps = cps(p.replacement || "");
        const opts = { caseSensitive: !!p.caseSensitive, wholeWord: !!p.wholeWord };
        const [rs, re] = scopeOf(p);
        const plan = [];
        for (let b = rs; b < re; b++) {
          const pos = matchesInCells(blocks[b].cells, q, opts);
          if (pos.length) plan.push([b, pos]);
        }
        let count = 0;
        if (plan.length) {
          snapshot();
          for (const [b, positions] of plan) {
            const cells = blocks[b].cells;
            // De droite à gauche : les positions à gauche restent valides.
            for (let i = positions.length - 1; i >= 0; i--) {
              cells.splice(positions[i], q.length, ...repCps.map((c) => ({ c, s: new Set() })));
              count++;
            }
          }
        }
        return { blocks: allBlocks(), count };
      },
      "editor.insertText": (p) => {
        snapshot();
        const chars = cps(p.text).map((c) => ({ c, s: new Set() }));
        blocks[p.block].cells.splice(p.pos, 0, ...chars);
        return { block: serialize(blocks[p.block]) };
      },
      "editor.removeText": (p) => {
        snapshot();
        blocks[p.block].cells.splice(p.pos, p.count);
        return { block: serialize(blocks[p.block]) };
      },
      "editor.applyStyle": (p) => {
        snapshot();
        const cells = blocks[p.block].cells;
        for (let i = p.pos; i < p.pos + p.count && i < cells.length; i++) cells[i].s.add(p.style);
        return { block: serialize(blocks[p.block]) };
      },
      "editor.removeStyle": (p) => {
        snapshot();
        const cells = blocks[p.block].cells;
        for (let i = p.pos; i < p.pos + p.count && i < cells.length; i++) cells[i].s.delete(p.style);
        return { block: serialize(blocks[p.block]) };
      },
      "editor.setFontSize": (p) => {
        snapshot();
        const cells = blocks[p.block].cells;
        const size = Math.max(20, Math.min(800, p.size | 0));
        for (let i = p.pos; i < p.pos + p.count && i < cells.length; i++) cells[i].sz = size;
        return { block: serialize(blocks[p.block]) };
      },
      "editor.toggleWordStyle": (p) => {
        const cells = blocks[p.block].cells;
        if (p.pos >= cells.length || isSep(cells[p.pos].c)) return { block: serialize(blocks[p.block]), changed: false };
        let a = p.pos, b = p.pos;
        while (a > 0 && !isSep(cells[a - 1].c)) a--;
        while (b < cells.length && !isSep(cells[b].c)) b++;
        snapshot();
        const already = cells[a].s.has(p.style);
        for (let i = a; i < b; i++) { if (already) cells[i].s.delete(p.style); else cells[i].s.add(p.style); }
        return { block: serialize(blocks[p.block]), changed: true };
      },
      "editor.setBlockType": (p) => { snapshot(); blocks[p.block].type = p.type; return { block: serialize(blocks[p.block]), blockIndex: p.block }; },
      "editor.splitBlock": (p) => {
        snapshot();
        const tail = blocks[p.block].cells.splice(p.pos);
        blocks.splice(p.block + 1, 0, { type: "p", cells: tail });
        return { blocks: allBlocks(), newBlock: p.block + 1 };
      },
      "editor.mergeBlock": (p) => {
        if (p.block === 0) return { blocks: allBlocks(), merged: false };
        snapshot();
        const caretPos = blocks[p.block - 1].cells.length;
        blocks[p.block - 1].cells.push(...blocks[p.block].cells);
        blocks.splice(p.block, 1);
        return { blocks: allBlocks(), merged: true, caretBlock: p.block - 1, caretPos };
      },
      "editor.moveBlocks": (p) => {
        // Miroir de Document::moveBlocks (C++).
        const n = blocks.length;
        let start = p.start, count = p.count, before = p.before;
        if (count === 0 || start >= n) return { blocks: allBlocks(), moved: false };
        count = Math.min(count, n - start);
        if (before > n) before = n;
        if (before >= start && before <= start + count) return { blocks: allBlocks(), moved: false };
        snapshot();
        const moved = blocks.splice(start, count);
        const insertAt = (before > start) ? before - count : before;
        blocks.splice(insertAt, 0, ...moved);
        return { blocks: allBlocks(), moved: true };
      },
      "editor.undo": () => {
        if (!undoStack.length) return { changed: false };
        redoStack.push(deepCopy());
        blocks = undoStack.pop();
        return { changed: true, wholeDocument: true, blocks: allBlocks() };
      },
      "editor.redo": () => {
        if (!redoStack.length) return { changed: false };
        undoStack.push(deepCopy());
        blocks = redoStack.pop();
        return { changed: true, wholeDocument: true, blocks: allBlocks() };
      },
      "editor.historyState": () => ({ canUndo: undoStack.length > 0, canRedo: redoStack.length > 0 }),
      "editor.breakUndoGroup": () => ({}),
      "editor.wordCount": () => ({ total: wordCount() }),
      // --- Fiches -----------------------------------------------------
      "cards.schemas": () => ({ schemas: JSON.parse(JSON.stringify(cardSchemas)) }),
      "cards.state": () => ({ root: cardsRoot, count: cardsById.size }),
      "cards.open": (p) => { cardsRoot = p.path || ""; return { root: cardsRoot, count: cardsById.size, skipped: 0 }; },
      // setRoot : fixe le dossier sans rien charger (les fiches en mémoire
      // « s'y trouvent » désormais). Le vrai C++ les écrit ; ici, en
      // mémoire, il suffit d'enregistrer le chemin.
      "cards.setRoot": (p) => { cardsRoot = p.path || ""; return { root: cardsRoot, count: cardsById.size }; },
      "cards.tags": () => {
        const t = new Set();
        cardsById.forEach((c) => c.tags.forEach((x) => t.add(x)));
        return { tags: [...t].sort() };
      },
      "cards.list": (p) => {
        let out = [...cardsById.values()];
        if (p.type) out = out.filter((c) => c.type === p.type);
        if (p.tag) out = out.filter((c) => c.tags.includes(p.tag));
        if (p.query) {
          const q = foldAccents(p.query);
          out = out.filter((c) => foldAccents(c.title || "").includes(q));
        }
        return { cards: out.sort(sortByTitle).map(summary) };
      },
      "cards.get": (p) => {
        const c = cardsById.get(p.id);
        if (!c) throw new Error("Fiche introuvable : " + p.id);
        return { card: cardCopy(c) };
      },
      "cards.save": (p) => {
        if (!cardSchemas.some((s) => s.typeId === p.type)) {
          throw new Error("Type de fiche inconnu : " + p.type);
        }
        let id = p.id || "";
        if (!id) {
          const base = slugifyJs(p.title);
          id = base;
          for (let n = 2; cardsById.has(id); n++) id = base + "-" + n;
        }
        const card = { id, type: p.type, title: p.title || "",
                       tags: (p.tags || []).slice(), fields: {} };
        for (const [k, v] of Object.entries(p.fields || {})) {
          const values = Array.isArray(v) ? v.filter((x) => typeof x === "string") : [String(v)];
          if (values.length) card.fields[k] = values;
        }
        cardsById.set(id, card);
        return { card: cardCopy(card) };
      },
      "cards.remove": (p) => {
        if (!cardsById.has(p.id)) return { removed: false };
        cardsById.delete(p.id);
        // Purge des références mortes, comme le C++.
        cardsById.forEach((c) => {
          for (const [k, v] of Object.entries(c.fields)) {
            const filtered = v.filter((x) => x !== p.id);
            if (filtered.length) c.fields[k] = filtered; else delete c.fields[k];
          }
        });
        return { removed: true };
      },
      "cards.saveAll": () => ({ saved: cardsById.size }),
      "cards.checkRefs": () => {
        const broken = [];
        cardsById.forEach((c) => {
          const sc = cardSchemas.find((s) => s.typeId === c.type);
          if (!sc) return;
          for (const def of sc.fields) {
            if (def.type !== "cardrefs") continue;
            for (const target of (c.fields[def.key] || [])) {
              if (!cardsById.has(target)) {
                broken.push({ id: c.id, title: c.title, type: c.type, field: def.key, fieldLabel: def.label, targetId: target });
              }
            }
          }
        });
        return { broken };
      },
      "cards.search": (p) => {
        const q = p.query || "";
        if (!q) return { results: [] };
        const cs = !!p.caseSensitive;
        const hit = (text) => cs ? (text || "").includes(q) : foldAccents(text || "").includes(foldAccents(q));
        const schemaOf = (t) => cardSchemas.find((s) => s.typeId === t);
        const results = [];
        [...cardsById.values()].sort(sortByTitle).forEach((c) => {
          const sc = schemaOf(c.type);
          if (hit(c.title)) results.push({ id: c.id, type: c.type, title: c.title, field: "titre", fieldLabel: "Titre", snippet: c.title });
          for (const [k, vals] of Object.entries(c.fields)) {
            const def = sc && sc.fields.find((f) => f.key === k);
            for (const v of vals) { if (hit(v)) { results.push({ id: c.id, type: c.type, title: c.title, field: k, fieldLabel: def ? def.label : k, snippet: v.slice(0, 90) }); break; } }
          }
        });
        return { results };
      },
      "cards.replaceAll": (p) => {
        const q = p.query || "", rep = p.replacement || "";
        if (!q) return { count: 0, cards: 0 };
        const cs = !!p.caseSensitive;
        const rx = new RegExp(q.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), cs ? "g" : "gi");
        let total = 0, touched = 0;
        cardsById.forEach((c) => {
          const sc = cardSchemas.find((s) => s.typeId === c.type);
          let n = 0;
          const nt = c.title.replace(rx, () => { n++; return rep; });
          if (n) c.title = nt;
          for (const [k, vals] of Object.entries(c.fields)) {
            const def = sc && sc.fields.find((f) => f.key === k);
            if (def && (def.type === "cardrefs" || def.type === "images")) continue;
            c.fields[k] = vals.map((v) => v.replace(rx, () => { n++; return rep; }));
          }
          if (n) { total += n; touched++; }
        });
        return { count: total, cards: touched };
      },

      "file.getMetadata": () => ({ ...meta }),
      "file.setMetadata": (p) => { ["title", "author", "cover", "summary"].forEach((k) => { if (k in p) meta[k] = p[k]; }); return { ...meta }; },
      "file.serialize": () => ({ markdown: "(aperçu simulé — sérialisation réelle côté C++)" }),
      "file.save": () => ({ saved: true }),
      "file.load": () => ({ loaded: true }),
      // Dialogues natifs : indisponibles dans le navigateur ; l'UI se
      // rabat de toute façon sur la saisie manuelle (hasNativeBridge).
      "file.chooseOpen": () => ({ cancelled: true }),
      "file.chooseSave": () => ({ cancelled: true }),
      "file.chooseFolder": () => ({ cancelled: true }),
      "file.backup": () => ({ backup: "(sauvegarde horodatée simulée)" }),
      // Le C++ réel mémorise cet état pour le garde-fou de fermeture.
      "file.setState": (p) => ({ dirty: !!p.dirty, path: p.path || "" }),

      // --- Export (Phase 4) -------------------------------------------
      // Rien de tout cela n'est simulable sans disque : l'aperçu
      // navigateur refuse proprement, et l'interface se rabat (pour le
      // PDF) sur la boîte d'impression du navigateur, qui produit un
      // résultat très proche.
      "export.epub": () => {
        throw new Error("Export EPUB indisponible dans l'aperçu (moteur simulé) : "
                        + "lancez l'application.");
      },
      "export.image": () => {
        throw new Error("Lecture d'image indisponible dans l'aperçu (moteur simulé).");
      },

      // --- Suivi de projet (Phase 5) ----------------------------------
      // Inventaire et journal supposent de vrais fichiers : dans
      // l'aperçu, on renvoie un état vide plutôt qu'une erreur, pour que
      // la sauvegarde simulée se déroule sans accroc.
      "project.state": () => ({ projectId: "", tracked: false, files: 0,
                                skipped: 0, pending: [] }),
      "project.snapshot": () => ({ projectId: "", files: 0, skipped: 0,
                                   recorded: [], compacted: 0 }),
      "project.history": () => ({ entries: [], total: 0 }),

      // --- Correcteur (fin de Phase 1) --------------------------------
      // Le vrai correcteur repose sur un dictionnaire de 20 Mo lu sur
      // disque : hors application, on répond « indisponible » et
      // l'interface n'affiche simplement aucun soulignement.
      "spell.state": () => ({ available: false, folder: "", loaded: false, forms: 0,
                              thesaurus: false, thesaurusEntries: 0, personal: [],
                              personalPath: "",
                              error: "Correcteur indisponible dans l'aperçu (moteur simulé)." }),
      "spell.load": () => ({ available: false, loaded: false, forms: 0, personal: [],
                             error: "Correcteur indisponible dans l'aperçu (moteur simulé)." }),
      "spell.check": () => ({ words: [], loaded: false }),
      "spell.suggest": (p) => ({ word: p.word || "", suggestions: [] }),
      "spell.synonyms": (p) => ({ word: p.word || "", meanings: [], loaded: false }),
      "spell.addWord": () => ({ added: false, saved: false, count: 0,
                                error: "Indisponible dans l'aperçu." }),
      "spell.removeWord": () => ({ removed: false, saved: false, count: 0 }),
      "spell.ignore": (p) => ({ ignored: p.word || "" }),
      "spell.excludeSynonym": () => {
        throw new Error("Correction indisponible dans l'aperçu (moteur simulé).");
      },

      // --- Comparaison de versions ------------------------------------
      // Lire un autre fichier sur le disque est hors de portée d'un
      // navigateur : on renvoie donc un RÉSULTAT D'EXEMPLE, qui rend
      // l'écran de comparaison accessible pour travailler sa mise en
      // page. Deux exemples, parce qu'il y a deux affichages à régler :
      // la comparaison ordinaire (paragraphe par paragraphe) et celle
      // d'une version qui a perdu son découpage (phrase par phrase).
      // Un chemin contenant « aplat » demande le second.
      "compare.withFile": (p) => {
        const cote = (type, text, marks) => ({ type: type, text: text, marks: marks || [] });
        const chemin = String(p.path || "version_precedente.md");
        if (/aplat/i.test(chemin)) {
          const titre = "Acte I : Le Destin de Cendres";
          return {
            path: chemin, title: "Exemple", identical: false, truncated: false,
            structure: { unit: "sentence", flattened: "right", match: 0.55 },
            stats: { equal: 2, changed: 1, added: 0, removed: 1,
                     wordsAdded: 6, wordsRemoved: 0 },
            rows: [
              { kind: "changed",
                left: cote("h1", titre),
                right: cote("p", titre + " Le vent soufflait contre les remparts.",
                            [[titre.length + 1, 38]]) },
              { kind: "removed",
                left: cote("p", "Le vent hurlait contre les remparts."), right: null },
              { kind: "equal", left: cote("p", "Regina veillait."),
                right: cote("p", "Regina veillait.") },
              { kind: "equal", left: cote("p", "Elle rêvait de forêts."),
                right: cote("p", "Elle rêvait de forêts.") },
            ],
          };
        }
        return {
          path: chemin, title: "Exemple", identical: false, truncated: false,
          structure: { unit: "block", flattened: "none", match: 0.92 },
          stats: { equal: 2, changed: 1, added: 1, removed: 0,
                   wordsAdded: 4, wordsRemoved: 0 },
          rows: [
            { kind: "equal", left: cote("h1", "Chapitre premier"),
              right: cote("h1", "Chapitre premier") },
            { kind: "changed", left: cote("p", "Il faisait nuit."),
              right: cote("p", "Il faisait nuit noire.", [[16, 5]]) },
            { kind: "added", left: null, right: cote("p", "Un chien aboya.") },
            { kind: "equal", left: cote("p", "Élise se leva."),
              right: cote("p", "Élise se leva.") },
          ],
        };
      },
      // Celle-ci MODIFIE le manuscrit : pas d'exemple qui vaille, on
      // refuse proprement plutôt que de simuler un redécoupage.
      "compare.reparagraph": () => {
        throw new Error("Redécoupage indisponible dans l'aperçu (moteur simulé) : "
                        + "lancez l'application.");
      },

      // --- Compte et appairage (Phase 5, jalon C) ---------------------
      // L'aperçu simule un appairage EN MÉMOIRE : de quoi régler l'écran
      // sans serveur. Aucune requête n'est émise, ce qui est d'ailleurs
      // exactement ce que fait l'application tant qu'on ne se connecte pas.
      "account.state": () => ({
        connected: mockAccount.connected,
        server: mockAccount.server,
        user: mockAccount.user,
        projectId: "aperçu",
        pairing: mockAccount.pairing,
      }),
      "account.signIn": (p) => {
        if (!p.server || !p.user) {
          return { connected: false, reason: "refuse",
                   message: "Serveur et identifiant sont nécessaires." };
        }
        mockAccount.connected = true;
        mockAccount.server = p.server;
        mockAccount.user = p.user;
        return { connected: true, server: p.server, user: p.user,
                 pairing: mockAccount.pairing };
      },
      "account.signOut": () => {
        mockAccount.connected = false;
        mockAccount.server = "";
        mockAccount.user = "";
        return { connected: false, transportReleased: true };
      },
      "account.pair": (p) => {
        mockAccount.pairing = { state: "appaire", mode: p.mode || "lecture-seule",
                                autoSync: false, server: mockAccount.server,
                                canPropose: false };
        return { pairing: mockAccount.pairing };
      },
      "account.unpair": () => {
        mockAccount.pairing = { state: "aucun", mode: "lecture-seule",
                                autoSync: false, server: "", canPropose: true };
        return { pairing: mockAccount.pairing };
      },
      "account.never": () => {
        mockAccount.pairing = { state: "jamais", mode: "lecture-seule",
                                autoSync: false, server: "", canPropose: false };
        return { pairing: mockAccount.pairing };
      },
      "account.setAutoSync": (p) => {
        mockAccount.pairing.autoSync = !!p.enabled;
        return { pairing: mockAccount.pairing };
      },
    };

    window.__mockBackend = {
      handle(command, payload) {
        const h = handlers[command];
        if (!h) return Promise.reject(new Error("Commande inconnue (mock) : " + command));
        // Asynchrone comme le vrai pont, pour exercer les mêmes chemins.
        // Le try/catch est indispensable : une exception lancée DANS le
        // setTimeout ne rejetterait pas la Promise, qui resterait en
        // attente pour toujours (l'appelant se figerait sans message).
        return new Promise((resolve, reject) => setTimeout(() => {
          try { resolve(h(payload || {})); } catch (err) { reject(err); }
        }, 0));
      },
    };
  })();
}

// =====================================================================
// 2. INTERFACE
// =====================================================================
//
// Le « use strict » est DANS la fonction, et non au-dessus : depuis que
// les deux parties partagent un même fichier, une directive placée ici
// ne serait plus en tête de script — donc plus une directive du tout,
// juste une expression sans effet. L'interface perdrait silencieusement
// le mode strict. À l'intérieur de la fonction, elle reste valable pour
// tout ce qu'elle contient, exactement comme avant le découpage.
(function () {
"use strict";

// =====================================================================
// 1. Pont : invoke(command, payload) -> Promise(result)
//    Route vers le C++ (window.cppInvoke) si présent, sinon vers le
//    backend simulé (voir tout en bas). Un rejet de Promise = erreur
//    métier/validation renvoyée par le backend.
// =====================================================================
const hasNativeBridge = typeof window.cppInvoke === "function";

async function invoke(command, payload = {}) {
  if (hasNativeBridge) {
    const raw = await window.cppInvoke(JSON.stringify({ command, payload }));
    // webview considère la valeur renvoyée par le C++ comme du JSON et
    // l'analyse LUI-MÊME : on reçoit donc déjà un objet. Certaines
    // versions transmettent la chaîne brute ; on accepte les deux plutôt
    // que de dépendre d'un détail d'implémentation de la librairie.
    // (Re-parser un objet donnait « "[object Object]" is not valid JSON »
    // et bloquait toute l'application.)
    const response = (typeof raw === "string") ? JSON.parse(raw) : raw;
    if (!response || typeof response !== "object") {
      throw new Error("Réponse inattendue du pont : " + String(raw));
    }
    if (!response.ok) throw new Error(response.error);
    return response.result;
  }
  // Backend simulé (développement / test hors webview).
  return window.__mockBackend.handle(command, payload);
}

// =====================================================================
// 2. Utilitaires points de code <-> UTF-16
//    Le modèle C++ compte en points de code ; le DOM en unités UTF-16.
//    Ces fonctions font le pont pour lire/poser le curseur correctement,
//    y compris pour les caractères hors BMP (émoji...).
// =====================================================================
function cpArray(str) { return Array.from(str); }
function cpLen(str) { return cpArray(str).length; }

// Décalage UTF-16 correspondant au n-ième point de code de 'str'.
function utf16FromCp(str, cp) {
  let i = 0, c = 0;
  while (c < cp && i < str.length) {
    i += str.codePointAt(i) > 0xffff ? 2 : 1;
    c++;
  }
  return i;
}

// =====================================================================
// 3. Frontières de mots (miroir de core::WordBoundary, pour la
//    suppression par mot et l'aperçu de la bascule de style).
// =====================================================================
const WORD_SEPARATORS = new Set([
  " ", "\t", "\n", "\r",
  ".", ",", ";", ":", "!", "?",
  "(", ")", "[", "]", "{", "}",
  '"', "«", "»",
]);
function isSeparator(ch) { return WORD_SEPARATORS.has(ch); }

// =====================================================================
// 4. État de la vue
//    'blocks' est le miroir de l'état renvoyé par le C++ : chaque bloc
//    est { type, text, runs:[{start,length,styles:[]}] }. On ne l'édite
//    jamais à la main : on le remplace par ce que renvoie le pont.
// =====================================================================
const state = {
  blocks: [],
  activeIndex: 0,
  // Curseur logique, tenu à jour de façon SYNCHRONE pendant la frappe
  // (les réponses du pont étant asynchrones, on ne peut pas se fier au
  // seul DOM pour enchaîner deux frappes rapides).
  caret: { block: 0, pos: 0 },
  zoom: 100,
  filePath: "",
  dirty: false, // modifications non enregistrées ?
};

const STYLE_NAMES = ["bold", "italic", "underline", "strikethrough", "superscript", "subscript"];

// File d'opérations : sérialise les appels au pont pour préserver l'ordre
// et éviter les entrelacements.
let opChain = Promise.resolve();
function enqueue(fn) {
  opChain = opChain.then(fn).catch(reportError);
  return opChain;
}
function drain() { return opChain.catch(() => {}); }

// =====================================================================
// 5. Rendu
// =====================================================================
const editorEl = document.getElementById("editor");
const docEl = document.getElementById("doc");

const HEADING_TAGS = { p: "P", h1: "H1", h2: "H2", h3: "H3", h4: "H4", h5: "H5", h6: "H6" };

// Construit, pour un bloc, la table style-par-caractère à partir des
// plages, puis regroupe les caractères consécutifs de même style en
// segments <span>/<sup>/<sub>.
// Table style/taille PAR CARACTÈRE, dépliée depuis les plages du modèle.
// Partagée par le rendu de l'éditeur et par celui de l'export (même
// apparence à l'écran et dans le livre : une seule logique).
function styleTable(block) {
  const cps = cpArray(block.text);
  const masks = cps.map(() => new Set());
  const sizes = cps.map(() => 100); // taille par caractère, 100 = normal
  for (const run of block.runs) {
    for (let k = 0; k < run.length; k++) {
      const idx = run.start + k;
      if (idx < masks.length) {
        run.styles.forEach((s) => masks[idx].add(s));
        if (run.size) sizes[idx] = run.size;
      }
    }
  }
  return { cps, masks, sizes };
}

function buildBlockChildren(block, index) {
  const { cps, masks, sizes } = styleTable(block);
  // Détection des mentions de fiches (Phase 3) : purement visuelle, jamais
  // écrite dans le modèle. On regroupe donc par (style, taille, mention).
  const mentions = detectMentions(cps);
  const blockOrder = chapterOrderOfBlock(index);
  // Mots inconnus du correcteur (fin de Phase 1) : une dimension de
  // regroupement de plus, au même titre que le style ou la mention.
  const faults = spellMarks(index, cps.length);

  const frag = document.createDocumentFragment();
  let i = 0;
  while (i < cps.length) {
    let j = i + 1;
    while (j < cps.length && sameSet(masks[i], masks[j]) &&
           sizes[i] === sizes[j] && mentions[i] === mentions[j] &&
           faults[i] === faults[j]) j++;
    const text = cps.slice(i, j).join("");
    const seg = makeSegment(text, masks[i], sizes[i]);
    if (faults[i]) seg.classList.add("faute");
    if (mentions[i]) {
      seg.classList.add("mention");
      seg.dataset.cardId = mentions[i];
      // Personnage mort dans un chapitre ANTÉRIEUR à celui-ci -> rouge.
      const deathOrder = mentionCtx.deathOrder.get(mentions[i]);
      if (deathOrder !== undefined && blockOrder > deathOrder) {
        seg.classList.add("mention-dead");
        seg.title = "Personnage mort dans un chapitre précédent — Ctrl+clic pour ouvrir la fiche";
      } else {
        seg.title = "Ctrl+clic pour ouvrir la fiche";
      }
    }
    frag.appendChild(seg);
    i = j;
  }
  return frag;
}

function sameSet(a, b) {
  if (a.size !== b.size) return false;
  for (const x of a) if (!b.has(x)) return false;
  return true;
}

function makeSegment(text, styleSet, sizePercent) {
  let el;
  if (styleSet.has("superscript")) el = document.createElement("sup");
  else if (styleSet.has("subscript")) el = document.createElement("sub");
  else el = document.createElement("span");

  const cls = [];
  if (styleSet.has("bold")) cls.push("s-bold");
  if (styleSet.has("italic")) cls.push("s-italic");
  if (cls.length) el.className = cls.join(" ");

  const deco = [];
  if (styleSet.has("underline")) deco.push("underline");
  if (styleSet.has("strikethrough")) deco.push("line-through");
  if (deco.length) el.style.textDecoration = deco.join(" ");

  // Taille relative (em) : se combine avec le zoom de lecture.
  if (sizePercent && sizePercent !== 100) {
    el.style.fontSize = (sizePercent / 100) + "em";
  }

  el.textContent = text;
  return el;
}

function makeBlockElement(block, index) {
  const el = document.createElement(HEADING_TAGS[block.type] || "P");
  el.className = "block";
  el.setAttribute("contenteditable", "true");
  el.spellcheck = false;
  el.dataset.index = index;
  if (index === 0) el.dataset.placeholder = "Commencez à écrire…";
  el.appendChild(buildBlockChildren(block, index));
  return el;
}

// --- Mentions de fiches (Phase 3) ------------------------------------
// Index des titres de fiches, trié du plus long au plus court pour qu'un
// titre englobant l'emporte (« Aria de Val » plutôt que « Aria »).
let mentionIndex = []; // [{ id, title, cps, deathChapter }]
// Contexte pour colorer les mentions « après la mort » : liste des
// chapitres et, par fiche décédée, l'ordre du chapitre de décès.
const mentionCtx = { chapters: [], deathOrder: new Map() };

async function refreshMentionIndex() {
  try {
    const r = await invoke("cards.list", {}); // toutes les fiches
    mentionIndex = (r.cards || [])
      .map((c) => ({ id: c.id, title: c.title || "", cps: cpArray((c.title || "").toLowerCase()),
                     deathChapter: c.deathChapter || "" }))
      .filter((m) => m.cps.length >= 2) // ignore les titres trop courts
      .sort((a, b) => b.cps.length - a.cps.length);
  } catch (_) { mentionIndex = []; }
  rebuildChapterCache();
}

// Recalcule la liste des chapitres et, pour chaque fiche ayant un chapitre
// de décès, l'ORDRE de ce chapitre (son rang dans le manuscrit). Une
// mention située dans un chapitre de rang supérieur est « après la mort ».
function rebuildChapterCache() {
  mentionCtx.chapters = computeChapters();
  const orderByTitle = new Map();
  mentionCtx.chapters.forEach((c, i) => { if (!orderByTitle.has(c.title)) orderByTitle.set(c.title, i); });
  mentionCtx.deathOrder = new Map();
  for (const m of mentionIndex) {
    if (m.deathChapter && orderByTitle.has(m.deathChapter)) {
      mentionCtx.deathOrder.set(m.id, orderByTitle.get(m.deathChapter));
    }
  }
}

// Rang du chapitre contenant un bloc (-1 si hors de tout chapitre).
function chapterOrderOfBlock(index) {
  const chs = mentionCtx.chapters;
  for (let i = 0; i < chs.length; i++) {
    if (index >= chs[i].start && index < chs[i].end) return i;
  }
  return -1;
}

// Pour un bloc (déjà découpé en points de code), retourne un tableau
// parallèle : identifiant de la fiche mentionnée à cette position, ou null.
// Détection insensible à la casse, sur des MOTS entiers uniquement.
function detectMentions(cps) {
  const marks = new Array(cps.length).fill(null);
  if (!mentionIndex.length || !cps.length) return marks;
  const lower = cps.map((c) => c.toLowerCase());
  for (const m of mentionIndex) {
    const t = m.cps;
    const L = t.length;
    for (let i = 0; i + L <= cps.length; i++) {
      if (marks[i] !== null) continue; // déjà pris par un titre plus long
      let ok = true;
      for (let k = 0; k < L; k++) { if (lower[i + k] !== t[k]) { ok = false; break; } }
      if (!ok) continue;
      const before = (i === 0) || isSeparator(cps[i - 1]);
      const after = (i + L === cps.length) || isSeparator(cps[i + L]);
      if (!before || !after) continue;
      let free = true;
      for (let k = 0; k < L; k++) { if (marks[i + k] !== null) { free = false; break; } }
      if (!free) continue;
      for (let k = 0; k < L; k++) marks[i + k] = m.id;
      i += L - 1;
    }
  }
  return marks;
}

// Rafraîchit l'index puis re-rend le manuscrit pour refléter les mentions
// (après un changement de fiches ou un chargement).
async function updateManuscriptMentions() {
  await refreshMentionIndex();
  if (document.getElementById("workspace").hidden) return; // pas en vue manuscrit
  const caret = { block: state.activeIndex, pos: state.caret.pos };
  renderAll();
  setCaret(caret.block, caret.pos);
}

// Vrai si 'needleLower' (points de code minuscules) apparaît en MOT ENTIER
// dans 'textCps'. Sert aux rétroliens et à la détection d'orphelines.
function textContainsTitle(textCps, needleLower) {
  const L = needleLower.length;
  if (!L || L > textCps.length) return false;
  const lower = textCps.map((c) => c.toLowerCase());
  for (let i = 0; i + L <= textCps.length; i++) {
    let ok = true;
    for (let k = 0; k < L; k++) { if (lower[i + k] !== needleLower[k]) { ok = false; break; } }
    if (!ok) continue;
    const before = (i === 0) || isSeparator(textCps[i - 1]);
    const after = (i + L === textCps.length) || isSeparator(textCps[i + L]);
    if (before && after) return true;
  }
  return false;
}

// Chapitre (titre H1) auquel appartient un bloc.
function chapterTitleOfBlock(chapters, blockIndex) {
  for (const c of chapters) {
    if (blockIndex >= c.start && blockIndex < c.end) return c.title;
  }
  return "(Manuscrit)";
}

// Rétroliens : pour chaque fiche mentionnée, l'ensemble des chapitres qui
// la citent. S'appuie sur l'index de mentions (à rafraîchir avant).
function computeBacklinks() {
  const chapters = computeChapters();
  const backlinks = new Map(); // id -> Set(titre de chapitre)
  state.blocks.forEach((b, i) => {
    const marks = detectMentions(cpArray(b.text));
    let ids = null;
    for (const m of marks) { if (m) { (ids || (ids = new Set())).add(m); } }
    if (!ids) return;
    const chap = chapterTitleOfBlock(chapters, i);
    ids.forEach((id) => {
      if (!backlinks.has(id)) backlinks.set(id, new Set());
      backlinks.get(id).add(chap);
    });
  });
  return backlinks;
}

// Chapitres qui mentionnent un titre donné (rétrolien d'une seule fiche,
// indépendant de l'index global — utilisé sur le formulaire de fiche).
function chaptersMentioning(title) {
  const needle = cpArray((title || "").toLowerCase());
  if (needle.length < 2) return [];
  const chapters = computeChapters();
  const found = new Set();
  state.blocks.forEach((b, i) => {
    if (textContainsTitle(cpArray(b.text), needle)) found.add(chapterTitleOfBlock(chapters, i));
  });
  return [...found];
}

// Re-rend tout le document (structure changée, chargement, undo global).
function renderAll() {
  docEl.textContent = "";
  state.blocks.forEach((b, i) => docEl.appendChild(makeBlockElement(b, i)));
}

// Re-rend le contenu d'un seul bloc (frappe, style) sans recréer le
// noeud (le type ne change pas), pour un rendu léger sur roman long.
function renderBlockContent(index) {
  const el = docEl.querySelector('.block[data-index="' + index + '"]');
  if (!el) return renderAll();
  el.textContent = "";
  el.appendChild(buildBlockChildren(state.blocks[index], index));
}

// =====================================================================
// 6. Curseur / sélection en positions caractère
// =====================================================================
function blockElAt(index) {
  return docEl.querySelector('.block[data-index="' + index + '"]');
}

function selectionInBlock(el) {
  const sel = window.getSelection();
  if (!sel.rangeCount) return null;
  const r = sel.getRangeAt(0);
  if (!el.contains(r.startContainer) || !el.contains(r.endContainer)) return null;
  const a = r.cloneRange(); a.selectNodeContents(el); a.setEnd(r.startContainer, r.startOffset);
  const b = r.cloneRange(); b.selectNodeContents(el); b.setEnd(r.endContainer, r.endOffset);
  return { start: cpLen(a.toString()), end: cpLen(b.toString()), collapsed: sel.isCollapsed };
}

function setCaret(index, cpPos) {
  const el = blockElAt(index);
  if (!el) return;
  el.focus({ preventScroll: true });
  placeCaret(el, cpPos);
}

function setSelectionRange(index, startCp, endCp) {
  const el = blockElAt(index);
  if (!el) return;
  el.focus({ preventScroll: true });
  const start = locateCp(el, startCp);
  const end = locateCp(el, endCp);
  const range = document.createRange();
  range.setStart(start.node, start.offset);
  range.setEnd(end.node, end.offset);
  const sel = window.getSelection();
  sel.removeAllRanges();
  sel.addRange(range);
}

function placeCaret(el, cpPos) {
  const loc = locateCp(el, cpPos);
  const range = document.createRange();
  range.setStart(loc.node, loc.offset);
  range.collapse(true);
  const sel = window.getSelection();
  sel.removeAllRanges();
  sel.addRange(range);
}

// Trouve le noeud texte et le décalage UTF-16 correspondant au cpPos-ième
// point de code dans le bloc. Se rabat sur la fin si cpPos déborde.
function locateCp(el, cpPos) {
  let remaining = cpPos;
  const walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
  let node, last = null;
  while ((node = walker.nextNode())) {
    last = node;
    const len = cpLen(node.data);
    if (remaining <= len) {
      return { node: node, offset: utf16FromCp(node.data, remaining) };
    }
    remaining -= len;
  }
  if (last) return { node: last, offset: last.data.length };
  return { node: el, offset: 0 };
}

// =====================================================================
// 7. Lookups sur le miroir local (style au curseur, mot sous le curseur)
// =====================================================================
function stylesAt(block, pos) {
  const set = new Set();
  for (const run of block.runs) {
    if (pos >= run.start && pos < run.start + run.length) {
      run.styles.forEach((s) => set.add(s));
    }
  }
  return set;
}

// Taille (en %) au caractère 'pos' (100 par défaut, hors de toute plage).
function sizeAt(block, pos) {
  for (const run of block.runs) {
    if (pos >= run.start && pos < run.start + run.length) {
      return run.size || 100;
    }
  }
  return 100;
}

// =====================================================================
// 8. Application d'un résultat du pont au miroir + rendu
// =====================================================================
// Toute modification passe par ces deux fonctions : c'est donc ici qu'on
// marque le document comme « modifié » (pour la sauvegarde automatique).
function applyBlockResult(index, result) {
  if (result && result.block) {
    state.blocks[index] = result.block;
    renderBlockContent(index);
    markDirty();
    scheduleSpellCheck(index); // vérification après une pause dans la frappe
  }
}
function applyWholeDocument(result) {
  if (result && result.blocks) {
    state.blocks = result.blocks;
    rebuildChapterCache(); // la structure (chapitres) a pu changer
    // Les indices de blocs ont bougé : les fautes retenues ne désignent
    // plus les bons blocs. On repart de zéro plutôt que de souligner à
    // côté.
    clearSpellRanges();
    renderAll();
    markDirty();
    checkAllBlocks();
  }
}

// =====================================================================
// 9. Saisie : interception de beforeinput
//    On empêche le navigateur de modifier le DOM lui-même : chaque geste
//    devient une commande du pont, puis le bloc est re-rendu depuis
//    l'état renvoyé par le C++.
// =====================================================================
editorEl.addEventListener("beforeinput", onBeforeInput);

// Composition (touches mortes : ê, î, ô, û, ï... très courantes en
// français). Pendant une composition, on laisse le navigateur éditer le
// DOM nativement, puis on réconcilie à la fin (compositionend) en
// renvoyant l'insertion au pont et en re-rendant depuis l'état C++.
let composing = false;
let compo = { block: 0, pos: 0 };

function onBeforeInput(e) {
  const el = e.target.closest && e.target.closest(".block");
  if (!el) return;
  const type = e.inputType;

  // Ne pas intercepter la composition : la bloquer casserait la saisie
  // des accents à touche morte.
  if (composing || type === "insertCompositionText" || type === "deleteCompositionText") {
    return;
  }

  const index = Number(el.dataset.index);
  const sel = selectionInBlock(el);

  // Cas gérés explicitement : on empêche systématiquement le
  // comportement natif et on route vers le pont.
  switch (type) {
    case "insertText":
    case "insertReplacementText":
      e.preventDefault();
      handleInsert(index, sel, e.data || "");
      return;
    case "insertParagraph":
      e.preventDefault();
      handleEnter(index, sel);
      return;
    case "insertLineBreak":
      // Modèle mono-ligne par bloc : Maj+Entrée = nouveau paragraphe.
      e.preventDefault();
      handleEnter(index, sel);
      return;
    case "deleteContentBackward":
      e.preventDefault();
      handleBackspace(index, sel);
      return;
    case "deleteContentForward":
      e.preventDefault();
      handleDelete(index, sel);
      return;
    case "deleteWordBackward":
      e.preventDefault();
      handleWordDelete(index, sel, true);
      return;
    case "deleteWordForward":
      e.preventDefault();
      handleWordDelete(index, sel, false);
      return;
    case "deleteByCut":
      e.preventDefault();
      if (sel && !sel.collapsed) removeRange(index, sel.start, sel.end, sel.start);
      return;
    case "insertFromPaste":
      // Le collage est traité par l'événement 'paste' (texte brut) : ici
      // on empêche seulement le comportement natif.
      e.preventDefault();
      return;
    default:
      // Tout autre type (historique, format natif...) : neutralisé, la
      // vue ne laisse jamais le DOM se modifier hors de notre contrôle.
      e.preventDefault();
      return;
  }
}

// Position d'une opération à curseur simple : on se fie au curseur
// LOGIQUE (tenu à jour de façon synchrone), jamais au DOM, qui peut être
// en retard pendant une frappe rapide (les réponses du pont sont
// asynchrones). Le DOM ne sert qu'à (re)synchroniser après un
// déplacement explicite du curseur (clic, flèches).
function collapsedPos(index, sel) {
  if (state.caret.block !== index) {
    state.caret = { block: index, pos: sel ? sel.start : 0 };
  }
  return state.caret.pos;
}

function handleInsert(index, sel, data) {
  if (!data) return;
  const insertLen = cpLen(data);

  if (sel && !sel.collapsed) {
    // Remplacement d'une sélection (l'utilisateur a sélectionné puis
    // tapé) : la file est au repos, le DOM est fiable.
    const start = sel.start, count = sel.end - sel.start;
    state.caret = { block: index, pos: start + insertLen };
    const caretTarget = state.caret.pos;
    enqueue(async () => {
      const r1 = await invoke("editor.removeText", { block: index, pos: start, count });
      applyBlockResult(index, r1);
      const r2 = await invoke("editor.insertText", { block: index, pos: start, text: data });
      applyBlockResult(index, r2);
      setCaret(index, caretTarget);
      refreshStatus();
    });
    return;
  }

  const pos = collapsedPos(index, sel);
  state.caret.pos = pos + insertLen; // avance SYNCHRONE (frappe rapide)
  const caretTarget = state.caret.pos;
  enqueue(async () => {
    const r = await invoke("editor.insertText", { block: index, pos, text: data });
    applyBlockResult(index, r);
    setCaret(index, caretTarget);
    refreshStatus();
  });
}

function handleBackspace(index, sel) {
  if (sel && !sel.collapsed) { removeRange(index, sel.start, sel.end, sel.start); return; }
  const pos = collapsedPos(index, sel);
  if (pos > 0) {
    state.caret.pos = pos - 1;
    enqueue(async () => {
      const r = await invoke("editor.removeText", { block: index, pos: pos - 1, count: 1 });
      applyBlockResult(index, r);
      setCaret(index, pos - 1);
      refreshStatus();
    });
  } else if (index > 0) {
    mergeInto(index); // début de bloc : fusion avec le précédent
  }
}

function handleDelete(index, sel) {
  if (sel && !sel.collapsed) { removeRange(index, sel.start, sel.end, sel.start); return; }
  const pos = collapsedPos(index, sel);
  const len = cpLen(state.blocks[index].text);
  if (pos < len) {
    state.caret.pos = pos;
    enqueue(async () => {
      const r = await invoke("editor.removeText", { block: index, pos: pos, count: 1 });
      applyBlockResult(index, r);
      setCaret(index, pos);
      refreshStatus();
    });
  } else if (index < state.blocks.length - 1) {
    mergeInto(index + 1); // fin de bloc : le bloc suivant remonte
  }
}

function handleWordDelete(index, sel, backward) {
  if (sel && !sel.collapsed) { removeRange(index, sel.start, sel.end, sel.start); return; }
  const cps = cpArray(state.blocks[index].text);
  const pos = collapsedPos(index, sel);
  let start = pos, end = pos;
  if (backward) {
    while (start > 0 && isSeparator(cps[start - 1])) start--;
    while (start > 0 && !isSeparator(cps[start - 1])) start--;
  } else {
    while (end < cps.length && isSeparator(cps[end])) end++;
    while (end < cps.length && !isSeparator(cps[end])) end++;
  }
  if (end > start) removeRange(index, start, end, start);
  else if (backward && index > 0 && pos === 0) mergeInto(index);
}

function removeRange(index, start, end, caretPos) {
  state.caret = { block: index, pos: caretPos };
  enqueue(async () => {
    const r = await invoke("editor.removeText", { block: index, pos: start, count: end - start });
    applyBlockResult(index, r);
    setCaret(index, caretPos);
    refreshStatus();
  });
}

function handleEnter(index, sel) {
  const pos = sel ? sel.start : state.caret.pos;
  enqueue(async () => {
    if (sel && !sel.collapsed) {
      const rr = await invoke("editor.removeText", { block: index, pos: sel.start, count: sel.end - sel.start });
      applyBlockResult(index, rr);
    }
    const r = await invoke("editor.splitBlock", { block: index, pos: pos });
    applyWholeDocument(r);
    const newBlock = r.newBlock != null ? r.newBlock : index + 1;
    state.caret = { block: newBlock, pos: 0 };
    state.activeIndex = newBlock;
    setCaret(newBlock, 0);
    refreshStatus();
  });
}

function mergeInto(index) {
  enqueue(async () => {
    const r = await invoke("editor.mergeBlock", { block: index });
    if (r.merged === false) return;
    applyWholeDocument(r);
    const cb = r.caretBlock != null ? r.caretBlock : index - 1;
    const cp = r.caretPos != null ? r.caretPos : 0;
    state.caret = { block: cb, pos: cp };
    state.activeIndex = cb;
    setCaret(cb, cp);
    refreshStatus();
  });
}

editorEl.addEventListener("compositionstart", () => {
  composing = true;
  compo = { block: state.activeIndex, pos: state.caret.pos };
});
editorEl.addEventListener("compositionend", (e) => {
  composing = false;
  const index = compo.block;
  const data = e.data || "";
  if (data) {
    state.caret = { block: index, pos: compo.pos + cpLen(data) };
    const caretTarget = state.caret.pos;
    enqueue(async () => {
      const r = await invoke("editor.insertText", { block: index, pos: compo.pos, text: data });
      applyBlockResult(index, r);
      setCaret(index, caretTarget);
      refreshStatus();
    });
  } else {
    // Composition annulée : le DOM a pu bouger, on le remet en cohérence.
    renderBlockContent(index);
    setCaret(index, compo.pos);
  }
});

// ---- Collage : texte brut uniquement (mono-ligne pour cette version) ----
editorEl.addEventListener("paste", (e) => {
  const el = e.target.closest && e.target.closest(".block");
  if (!el) return;
  e.preventDefault();
  const index = Number(el.dataset.index);
  const sel = selectionInBlock(el);
  const clip = (e.clipboardData || window.clipboardData).getData("text/plain") || "";
  // Limite documentée : les sauts de ligne d'un collage sont convertis en
  // espaces (pas encore de découpage en plusieurs blocs au collage).
  const text = clip.replace(/\s*\r?\n\s*/g, " ");
  if (text) handleInsert(index, sel, text);
});

// ---- Couper : copie native puis suppression via le pont ----
editorEl.addEventListener("cut", (e) => {
  const el = e.target.closest && e.target.closest(".block");
  if (!el) return;
  const sel = selectionInBlock(el);
  if (!sel || sel.collapsed) { e.preventDefault(); return; }
  const cps = cpArray(state.blocks[Number(el.dataset.index)].text);
  const selected = cps.slice(sel.start, sel.end).join("");
  e.clipboardData.setData("text/plain", selected);
  e.preventDefault();
  removeRange(Number(el.dataset.index), sel.start, sel.end, sel.start);
});

// =====================================================================
// 10. Suivi du curseur : bloc actif + clôture des groupes d'annulation
//     La frappe ne clôt PAS le groupe (regroupement par mot) ; seuls les
//     déplacements explicites du curseur le font (clic, flèches...).
// =====================================================================
document.addEventListener("selectionchange", () => {
  const sel = window.getSelection();
  if (!sel.rangeCount) return;
  const el = sel.getRangeAt(0).startContainer;
  const block = el.nodeType === 1 ? el.closest(".block") : el.parentElement && el.parentElement.closest(".block");
  if (!block) return;
  const index = Number(block.dataset.index);
  if (index !== state.activeIndex) {
    state.activeIndex = index;
    updateBlockTypeSelect();
  }
  updateStyleButtons();
});

// Ctrl+clic sur une mention -> ouvre la fiche correspondante (Phase 3).
// On garde le clic simple pour l'édition (placer le curseur) ; le Ctrl
// évite d'interférer avec l'écriture, à la manière des liens d'un IDE.
editorEl.addEventListener("click", (e) => {
  if (!(e.ctrlKey || e.metaKey)) return;
  const mention = e.target.closest && e.target.closest(".mention");
  if (!mention || !mention.dataset.cardId) return;
  e.preventDefault();
  openCardFromManuscript(mention.dataset.cardId);
});

async function openCardFromManuscript(id) {
  setMode("fiches");
  await initCardsView();  // idempotent : charge les schémas au besoin
  await selectCard(id);
}

function breakGroupSoon() {
  // Déplacement explicite du curseur -> le prochain caractère tapé
  // démarre un nouveau groupe d'annulation.
  enqueue(() => invoke("editor.breakUndoGroup", {}));
}

editorEl.addEventListener("mouseup", () => {
  syncCaretFromDom();
  breakGroupSoon();
});
editorEl.addEventListener("keydown", (e) => {
  // Navigation entre blocs : chaque bloc étant un contenteditable séparé,
  // le navigateur ne franchit pas leurs frontières tout seul. On détecte
  // les cas de bord et on déplace le curseur d'un bloc à l'autre.
  if (handleArrowNav(e)) return;
  const nav = ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End", "PageUp", "PageDown"];
  if (nav.includes(e.key)) {
    // Déplacement interne au bloc : géré nativement. On resynchronise
    // ensuite notre position logique et on clôt le groupe d'annulation.
    setTimeout(() => { syncCaretFromDom(); breakGroupSoon(); }, 0);
  }
});

function syncCaretFromDom() {
  const sel = window.getSelection();
  if (!sel.rangeCount) return;
  const node = sel.getRangeAt(0).startContainer;
  const el = node.nodeType === 1 ? node.closest && node.closest(".block")
                                 : node.parentElement && node.parentElement.closest(".block");
  if (!el) return;
  const index = Number(el.dataset.index);
  const s = selectionInBlock(el);
  if (s) { state.caret = { block: index, pos: s.start }; state.activeIndex = index; }
}

// Rectangle (en coordonnées écran) du curseur courant, pour décider si on
// est sur la première/dernière ligne visuelle d'un bloc (texte pouvant
// être replié sur plusieurs lignes).
function caretClientRect(el) {
  const sel = window.getSelection();
  if (sel.rangeCount) {
    const r = sel.getRangeAt(0).cloneRange();
    r.collapse(true);
    const rect = r.getBoundingClientRect();
    if (rect && (rect.height || rect.top || rect.left)) return rect;
  }
  // Bloc vide : pas de rectangle de curseur exploitable, on prend le bloc.
  const b = el.getBoundingClientRect();
  const lh = parseFloat(getComputedStyle(el).lineHeight) || b.height || 20;
  return { top: b.top, bottom: b.top + lh, left: b.left, right: b.left, height: lh };
}

function currentBlockFromSelection() {
  const sel = window.getSelection();
  if (!sel.rangeCount) return null;
  const node = sel.getRangeAt(0).startContainer;
  const el = node.nodeType === 1 ? node.closest && node.closest(".block")
                                 : node.parentElement && node.parentElement.closest(".block");
  return el || null;
}

// Retourne true si la flèche a provoqué un changement de bloc (et a été
// traitée), false si le déplacement reste interne au bloc (natif).
function handleArrowNav(e) {
  const key = e.key;
  if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(key)) return false;
  // Avec Maj (sélection), Ctrl/Alt (saut de mot...) : comportement natif.
  if (e.shiftKey || e.ctrlKey || e.altKey || e.metaKey) return false;

  const el = currentBlockFromSelection();
  if (!el) return false;
  const index = Number(el.dataset.index);
  const sel = selectionInBlock(el);
  if (!sel || !sel.collapsed) return false; // une sélection se réduit nativement
  const pos = sel.start;
  const len = cpLen(state.blocks[index].text);

  if (key === "ArrowLeft" && pos === 0) {
    if (index === 0) return false;
    e.preventDefault();
    moveCaretToBlock(index - 1, cpLen(state.blocks[index - 1].text)); // fin du bloc précédent
    return true;
  }
  if (key === "ArrowRight" && pos === len) {
    if (index >= state.blocks.length - 1) return false;
    e.preventDefault();
    moveCaretToBlock(index + 1, 0); // début du bloc suivant
    return true;
  }
  if (key === "ArrowUp" || key === "ArrowDown") {
    const cr = caretClientRect(el);
    const br = el.getBoundingClientRect();
    const lh = cr.height || 20;
    const onFirstLine = (cr.top - br.top) < lh * 0.6;
    const onLastLine = (br.bottom - cr.bottom) < lh * 0.6;
    if (key === "ArrowUp" && onFirstLine) {
      if (index === 0) return false;
      e.preventDefault();
      moveCaretVertically(index - 1, cr.left, "bottom", lh); // dernière ligne du bloc au-dessus
      return true;
    }
    if (key === "ArrowDown" && onLastLine) {
      if (index >= state.blocks.length - 1) return false;
      e.preventDefault();
      moveCaretVertically(index + 1, cr.left, "top", lh); // première ligne du bloc en dessous
      return true;
    }
  }
  return false;
}

function moveCaretToBlock(index, cpPos) {
  state.activeIndex = index;
  state.caret = { block: index, pos: cpPos };
  setCaret(index, cpPos);
  updateBlockTypeSelect();
  breakGroupSoon();
}

// Place le curseur dans le bloc cible en conservant la colonne (x) du
// curseur d'origine, sur la première ou la dernière ligne selon 'edge'.
function moveCaretVertically(index, x, edge, lh) {
  const el = blockElAt(index);
  if (!el) return;
  el.focus({ preventScroll: false });
  const br = el.getBoundingClientRect();
  const clampedX = Math.max(br.left + 1, Math.min(x, br.right - 1));
  const y = edge === "top" ? br.top + lh * 0.5 : br.bottom - lh * 0.5;

  let range = null;
  if (document.caretRangeFromPoint) {
    range = document.caretRangeFromPoint(clampedX, y);
  } else if (document.caretPositionFromPoint) {
    const cp = document.caretPositionFromPoint(clampedX, y);
    if (cp) { range = document.createRange(); range.setStart(cp.offsetNode, cp.offset); }
  }
  if (range && el.contains(range.startContainer)) {
    range.collapse(true);
    const sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
  } else {
    // Repli : début ou fin du bloc cible.
    placeCaret(el, edge === "top" ? 0 : cpLen(state.blocks[index].text));
  }
  state.activeIndex = index;
  syncCaretFromDom();
  updateBlockTypeSelect();
  breakGroupSoon();
}

// =====================================================================
// 11. Raccourcis clavier (configurables, cf. cadrage : modifiables avec
//     vérification qu'un raccourci n'est pas déjà utilisé)
// =====================================================================
// Actions déclenchables : libellé (pour les paramètres) + fonction.
const ACTIONS = {
  bold:          { label: "Gras",         run: () => toggleStyleFromToolbar("bold") },
  italic:        { label: "Italique",     run: () => toggleStyleFromToolbar("italic") },
  underline:     { label: "Souligné",     run: () => toggleStyleFromToolbar("underline") },
  strikethrough: { label: "Barré",        run: () => toggleStyleFromToolbar("strikethrough") },
  superscript:   { label: "Exposant",     run: () => toggleStyleFromToolbar("superscript") },
  subscript:     { label: "Indice",       run: () => toggleStyleFromToolbar("subscript") },
  undo:          { label: "Annuler",      run: () => doUndo() },
  redo:          { label: "Rétablir",     run: () => doRedo() },
  find:          { label: "Rechercher",   run: () => openFindBar() },
  save:          { label: "Enregistrer",  run: () => doSaveFlow() },
  open:          { label: "Ouvrir",       run: () => doOpenFlow() },
  metadata:      { label: "Métadonnées",  run: () => openMetaDialog() },
  exportBook:    { label: "Exporter",     run: () => openExportDialog() },
};

// Raccourcis par défaut. Certains sont laissés vides : à l'utilisateur de
// les définir s'il le souhaite (ex : exposant/indice).
const DEFAULT_SHORTCUTS = {
  bold: "ctrl+b", italic: "ctrl+i", underline: "ctrl+u",
  strikethrough: "", superscript: "", subscript: "",
  undo: "ctrl+z", redo: "ctrl+y", find: "ctrl+f",
  save: "ctrl+s", open: "ctrl+o", metadata: "",
  // Ctrl+P ouvre NOTRE dialogue d'export plutôt que la boîte
  // d'impression du navigateur : c'est le même geste, mais avec les
  // options du livre (couverture, format, table des matières).
  exportBook: "ctrl+p",
};

let shortcuts = loadShortcuts();

function loadShortcuts() {
  const merged = { ...DEFAULT_SHORTCUTS };
  try {
    const saved = JSON.parse(localStorage.getItem("editeur.shortcuts") || "{}");
    for (const k of Object.keys(merged)) if (typeof saved[k] === "string") merged[k] = saved[k];
  } catch (_) {}
  return merged;
}
function saveShortcuts() {
  try { localStorage.setItem("editeur.shortcuts", JSON.stringify(shortcuts)); } catch (_) {}
}

// Combinaison canonique depuis un événement clavier (ex : "ctrl+shift+z").
// Retourne null pour une touche de modificateur seule.
function comboFromEvent(e) {
  const key = e.key;
  if (["Control", "Shift", "Alt", "Meta", "OS"].includes(key)) return null;
  const parts = [];
  if (e.ctrlKey || e.metaKey) parts.push("ctrl");
  if (e.altKey) parts.push("alt");
  if (e.shiftKey) parts.push("shift");
  parts.push(key.length === 1 ? key.toLowerCase() : key.toLowerCase());
  return parts.join("+");
}

// Affichage lisible d'une combinaison ("ctrl+shift+z" -> "Ctrl+Maj+Z").
function formatCombo(combo) {
  if (!combo) return "—";
  return combo.split("+").map((p) => {
    if (p === "ctrl") return "Ctrl";
    if (p === "alt") return "Alt";
    if (p === "shift") return "Maj";
    if (p === "arrowup") return "↑";
    if (p === "arrowdown") return "↓";
    if (p === "arrowleft") return "←";
    if (p === "arrowright") return "→";
    return p.length === 1 ? p.toUpperCase() : (p.charAt(0).toUpperCase() + p.slice(1));
  }).join("+");
}

function actionForCombo(combo) {
  if (!combo) return null;
  for (const action of Object.keys(shortcuts)) {
    if (shortcuts[action] && shortcuts[action] === combo) return action;
  }
  return null;
}

let recordingAction = null; // action en cours de réattribution (paramètres)

// Gestionnaire principal : déclenche l'action associée à la combinaison.
// N'agit que si un modificateur Ctrl/Alt est présent (ne gêne jamais la
// frappe normale). Ctrl+C/V/X restent gérés nativement / via paste-cut.
document.addEventListener("keydown", (e) => {
  if (recordingAction) return; // la capture des paramètres a la priorité
  if (!(e.ctrlKey || e.metaKey || e.altKey)) return;
  const action = actionForCombo(comboFromEvent(e));
  if (action && ACTIONS[action]) {
    e.preventDefault();
    ACTIONS[action].run();
  }
});

// =====================================================================
// 12. Barre d'outils : styles, type de bloc, undo/redo, zoom, thème
// =====================================================================
// Empêche les boutons de voler le focus (donc la sélection) à l'éditeur.
document.querySelectorAll("#toolbar button").forEach((b) => {
  b.addEventListener("mousedown", (e) => e.preventDefault());
});

document.querySelectorAll(".style-btn").forEach((btn) => {
  btn.addEventListener("click", () => toggleStyleFromToolbar(btn.dataset.style));
});

function toggleStyleFromToolbar(style) {
  const index = state.activeIndex;
  const el = blockElAt(index);
  if (!el) return;
  const sel = selectionInBlock(el);
  enqueue(async () => {
    if (sel && !sel.collapsed) {
      // Bascule d'après le premier caractère de la sélection.
      const already = stylesAt(state.blocks[index], sel.start).has(style);
      const cmd = already ? "editor.removeStyle" : "editor.applyStyle";
      const r = await invoke(cmd, { block: index, pos: sel.start, count: sel.end - sel.start, style });
      applyBlockResult(index, r);
      setSelectionRange(index, sel.start, sel.end);
    } else {
      // Sans sélection : bascule le mot entier sous le curseur.
      const pos = sel ? sel.start : state.caret.pos;
      const r = await invoke("editor.toggleWordStyle", { block: index, pos, style });
      applyBlockResult(index, r);
      setCaret(index, pos);
    }
    updateStyleButtons();
    refreshStatus();
  });
}

// ---- Taille des caractères (agit sur le texte SÉLECTIONNÉ) ----
const FS_MIN = 20, FS_MAX = 800, FS_STEP = 10;
const fontSizeInput = document.getElementById("fontSizeInput");

// Mot (suite de caractères non séparateurs) contenant 'pos', ou null si le
// curseur est sur un séparateur ou en fin de bloc.
function wordRangeInBlock(block, pos) {
  const cps = cpArray(block.text);
  if (pos >= cps.length || isSeparator(cps[pos])) return null;
  let a = pos, b = pos;
  while (a > 0 && !isSeparator(cps[a - 1])) a--;
  while (b < cps.length && !isSeparator(cps[b])) b++;
  return { start: a, end: b };
}

// Plage visée par un changement de taille : la sélection vive si elle
// existe, sinon le mot sous le curseur (même ergonomie que le gras). Vaut
// pour les boutons A− / A+, qui NE volent PAS le focus à l'éditeur.
function fontSizeTargetRange() {
  const index = state.activeIndex;
  const el = blockElAt(index);
  if (el) {
    const sel = selectionInBlock(el);
    if (sel && !sel.collapsed) return { index, start: sel.start, end: sel.end };
  }
  const block = state.blocks[index];
  if (block) {
    const w = wordRangeInBlock(block, state.caret.pos);
    if (w) return { index, start: w.start, end: w.end };
  }
  return null;
}

// Le CHAMP de saisie, lui, prend le focus : la sélection de l'éditeur est
// alors vidée. On la capture donc au 'mousedown' du champ, AVANT le
// basculement du focus, et c'est cette plage figée qu'on applique ensuite.
//
// Important : on ne met à jour la capture QUE si l'éditeur détient encore
// une position (selectionInBlock non nul). Sinon on ne touche à rien — ce
// qui protège des mousedown suivants d'un multi-clic sur le champ, qui
// arrivent alors que le focus est déjà parti et écraseraient la capture.
let fontSizeCapturedRange = null;
function captureFontSizeRange() {
  const index = state.activeIndex;
  const el = blockElAt(index);
  if (!el) return;
  const sel = selectionInBlock(el);
  if (!sel) return; // focus déjà hors éditeur : on garde la capture précédente
  if (!sel.collapsed) {
    fontSizeCapturedRange = { index, start: sel.start, end: sel.end };
  } else {
    // Caret sans sélection : le mot sous le curseur (ergonomie du gras).
    const block = state.blocks[index];
    const w = block && wordRangeInBlock(block, sel.start);
    fontSizeCapturedRange = w ? { index, start: w.start, end: w.end } : null;
  }
}

const clampFontSize = (v) => Math.max(FS_MIN, Math.min(FS_MAX, Math.round(v)));

// Applique une taille absolue (en %) à une plage donnée, puis restaure la
// sélection dans l'éditeur (le champ de saisie avait pu la vider).
function applyFontSizeToRange(target, percent) {
  if (!target || target.start === target.end) return;
  const size = clampFontSize(percent);
  enqueue(async () => {
    const r = await invoke("editor.setFontSize", {
      block: target.index, pos: target.start,
      count: target.end - target.start, size,
    });
    applyBlockResult(target.index, r);
    setSelectionRange(target.index, target.start, target.end);
    updateFontSizeInput();
    refreshStatus();
  });
}

// Agrandit/réduit d'un cran, à partir de la taille du début de la plage
// (arrondie au pas pour que les crans restent réguliers).
function changeFontSizeBy(delta) {
  const target = fontSizeTargetRange();
  if (!target || target.start === target.end) return;
  const current = sizeAt(state.blocks[target.index], target.start) || 100;
  const stepped = Math.round((current + delta) / FS_STEP) * FS_STEP;
  applyFontSizeToRange(target, stepped);
}

function commitFontSizeInput() {
  const v = Number(fontSizeInput.value);
  const target = fontSizeCapturedRange;
  fontSizeCapturedRange = null; // consommé : évite un double-commit (Entrée puis blur)
  if (!Number.isFinite(v) || v <= 0 || !target || target.start === target.end) {
    updateFontSizeInput();
    return;
  }
  applyFontSizeToRange(target, v);
}

// Reflète dans le champ la taille au niveau du curseur/de la sélection
// (sauf pendant que l'utilisateur y tape).
function updateFontSizeInput() {
  if (document.activeElement === fontSizeInput) return;
  const block = state.blocks[state.activeIndex];
  if (!block) { fontSizeInput.value = 100; return; }
  const el = blockElAt(state.activeIndex);
  let pos = Math.max(0, state.caret.pos - 1);
  if (el) {
    const sel = selectionInBlock(el);
    if (sel && !sel.collapsed) pos = sel.start;
  }
  fontSizeInput.value = sizeAt(block, pos) || 100;
}

document.getElementById("fontSizeUp").addEventListener("click", () => changeFontSizeBy(FS_STEP));
document.getElementById("fontSizeDown").addEventListener("click", () => changeFontSizeBy(-FS_STEP));
// Capture la sélection AVANT que le champ ne prenne le focus (mousedown
// précède le basculement de focus, contrairement à 'focus').
fontSizeInput.addEventListener("mousedown", captureFontSizeRange);
fontSizeInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") { e.preventDefault(); commitFontSizeInput(); fontSizeInput.blur(); }
});
fontSizeInput.addEventListener("change", commitFontSizeInput);

const blockTypeSelect = document.getElementById("blockType");
blockTypeSelect.addEventListener("change", () => {
  const index = state.activeIndex;
  const type = blockTypeSelect.value;
  const pos = state.caret.pos;
  enqueue(async () => {
    const r = await invoke("editor.setBlockType", { block: index, type });
    // Le type change la balise du bloc : re-rendu complet, plus simple.
    if (r && r.block) state.blocks[index] = r.block;
    renderAll();
    setCaret(index, pos);
    refreshStatus();
  });
});

document.getElementById("undoBtn").addEventListener("click", doUndo);
document.getElementById("redoBtn").addEventListener("click", doRedo);

function doUndo() {
  enqueue(async () => {
    const r = await invoke("editor.undo", {});
    if (!r.changed) return;
    if (r.wholeDocument) { applyWholeDocument(r); state.caret = { block: 0, pos: 0 }; }
    else { applyBlockResult(r.blockIndex, r); state.activeIndex = r.blockIndex; }
    focusActive();
    refreshStatus();
  });
}
function doRedo() {
  enqueue(async () => {
    const r = await invoke("editor.redo", {});
    if (!r.changed) return;
    if (r.wholeDocument) applyWholeDocument(r);
    else { applyBlockResult(r.blockIndex, r); state.activeIndex = r.blockIndex; }
    focusActive();
    refreshStatus();
  });
}

function focusActive() {
  const el = blockElAt(state.activeIndex) || blockElAt(0);
  if (el) el.focus({ preventScroll: true });
}

// ---- Zoom (boutons + Ctrl+molette), confort de lecture ----
function setZoom(z) {
  state.zoom = Math.max(60, Math.min(240, Math.round(z)));
  document.documentElement.style.setProperty("--editor-font-size", (18 * state.zoom / 100).toFixed(1) + "px");
  document.getElementById("zoomLabel").textContent = state.zoom + " %";
  try { localStorage.setItem("editeur.zoom", String(state.zoom)); } catch (_) {}
}
document.getElementById("zoomIn").addEventListener("click", () => setZoom(state.zoom + 10));
document.getElementById("zoomOut").addEventListener("click", () => setZoom(state.zoom - 10));
document.getElementById("zoomReset").addEventListener("click", () => setZoom(100));
editorEl.addEventListener("wheel", (e) => {
  if (!(e.ctrlKey || e.metaKey)) return;
  e.preventDefault();
  setZoom(state.zoom + (e.deltaY < 0 ? 10 : -10));
}, { passive: false });

// ---- Thème ----
const themeSelect = document.getElementById("themeSelect");
function setTheme(name) {
  document.documentElement.dataset.theme = name;
  themeSelect.value = name;
  try { localStorage.setItem("editeur.theme", name); } catch (_) {}
}
themeSelect.addEventListener("change", () => setTheme(themeSelect.value));

// =====================================================================
// 13. Barre d'état : compteur de mots (total + chapitre), historique
// =====================================================================
async function refreshStatus() {
  // Compteur de mots : total via le pont (implémentation C++ de
  // référence) ; répartition par chapitre calculée côté vue.
  let total = 0;
  try { total = (await invoke("editor.wordCount", {})).total; }
  catch (_) { total = localWordCount(); }
  document.getElementById("wordcount").textContent =
    total.toLocaleString("fr-FR") + (total > 1 ? " mots" : " mot");

  const chapters = countChapters();
  const chapWords = currentChapterWords();
  document.getElementById("chapterCount").textContent =
    chapters + (chapters > 1 ? " chapitres" : " chapitre") + " · chapitre courant : " + chapWords + (chapWords > 1 ? " mots" : " mot");

  try {
    const h = await invoke("editor.historyState", {});
    document.getElementById("undoBtn").disabled = !h.canUndo;
    document.getElementById("redoBtn").disabled = !h.canRedo;
  } catch (_) {}

  renderChapters(false); // met à jour le plan (rebuild seulement si changé)
}

function wordsIn(text) {
  let n = 0, inWord = false;
  for (const ch of text) {
    if (isSeparator(ch)) inWord = false;
    else if (!inWord) { inWord = true; n++; }
  }
  return n;
}
function localWordCount() { return state.blocks.reduce((s, b) => s + wordsIn(b.text), 0); }
// Un chapitre = un titre H1 (et les blocs qui le suivent). Les blocs
// avant le premier H1 comptent comme un chapitre implicite s'ils existent.
function countChapters() {
  let chapters = 0, sawContentBeforeH1 = false;
  state.blocks.forEach((b) => {
    if (b.type === "h1") chapters++;
    else if (chapters === 0 && b.text.trim() !== "") sawContentBeforeH1 = true;
  });
  return Math.max(1, chapters + (sawContentBeforeH1 && chapters === 0 ? 1 : 0));
}
function currentChapterWords() {
  let startIdx = 0;
  for (let i = state.activeIndex; i >= 0; i--) {
    if (state.blocks[i] && state.blocks[i].type === "h1") { startIdx = i; break; }
  }
  let words = 0;
  for (let i = startIdx; i < state.blocks.length; i++) {
    if (i > startIdx && state.blocks[i].type === "h1") break;
    words += wordsIn(state.blocks[i].text);
  }
  return words;
}

function updateStyleButtons() {
  const block = state.blocks[state.activeIndex];
  if (!block) return;
  const set = stylesAt(block, Math.max(0, state.caret.pos - 1));
  const setAt = stylesAt(block, state.caret.pos);
  document.querySelectorAll(".style-btn").forEach((btn) => {
    const s = btn.dataset.style;
    btn.classList.toggle("active", set.has(s) || setAt.has(s));
  });
  updateFontSizeInput();
}
function updateBlockTypeSelect() {
  const block = state.blocks[state.activeIndex];
  if (block) blockTypeSelect.value = block.type;
}

// =====================================================================
// 14. Métadonnées & fichiers
// =====================================================================
const metaDialog = document.getElementById("metaDialog");
async function openMetaDialog() {
  try {
    const m = await invoke("file.getMetadata", {});
    document.getElementById("metaTitle").value = m.title || "";
    document.getElementById("metaAuthor").value = m.author || "";
    document.getElementById("metaCover").value = m.cover || "";
    document.getElementById("metaSummary").value = m.summary || "";
    metaDialog.showModal();
  } catch (err) { reportError(err); }
}
document.getElementById("metaBtn").addEventListener("click", openMetaDialog);
document.getElementById("metaCancel").addEventListener("click", () => metaDialog.close());
document.getElementById("metaSave").addEventListener("click", async () => {
  try {
    await invoke("file.setMetadata", {
      title: document.getElementById("metaTitle").value,
      author: document.getElementById("metaAuthor").value,
      cover: document.getElementById("metaCover").value,
      summary: document.getElementById("metaSummary").value,
    });
    metaDialog.close();
    markDirty(); // les métadonnées font partie du document
    setStatus("Métadonnées enregistrées.");
  } catch (err) { reportError(err); }
});

const pathDialog = document.getElementById("pathDialog");
let pathResolve = null; // résout la Promise de askPath (repli navigateur)

// --- Confirmation générique (retourne une Promise<boolean>) -----------
// On n'utilise pas window.confirm : son comportement n'est pas garanti
// dans un webview. On s'appuie sur le <dialog> natif de la page.
const confirmDialog = document.getElementById("confirmDialog");
let confirmResolve = null;
// Retourne true (bouton principal) ou false (annuler). Si 'thirdLabel'
// est fourni, un troisième bouton apparaît et sa sélection retourne la
// chaîne "third" — les appels à trois arguments gardent donc exactement
// leur comportement d'origine.
function askConfirm(message, okLabel, title, thirdLabel) {
  return new Promise((resolve) => {
    document.getElementById("confirmTitle").textContent = title || "Confirmer";
    document.getElementById("confirmMsg").textContent = message;
    document.getElementById("confirmOk").textContent = okLabel || "Continuer";
    const third = document.getElementById("confirmThird");
    third.hidden = !thirdLabel;
    if (thirdLabel) third.textContent = thirdLabel;
    confirmResolve = resolve;
    confirmDialog.showModal();
  });
}
function closeConfirm(result) {
  confirmDialog.close();
  if (confirmResolve) { confirmResolve(result); confirmResolve = null; }
}
document.getElementById("confirmCancel").addEventListener("click", () => closeConfirm(false));
document.getElementById("confirmOk").addEventListener("click", () => closeConfirm(true));
document.getElementById("confirmThird").addEventListener("click", () => closeConfirm("third"));
// Fermeture par Échap : équivaut à Annuler.
confirmDialog.addEventListener("cancel", (e) => { e.preventDefault(); closeConfirm(false); });

// Nombre de fiches en mémoire qui seraient PERDUES si on remplaçait la
// bibliothèque : uniquement celles jamais écrites sur disque (aucun
// dossier rattaché). Si un dossier existe, les fiches y sont déjà sauvées.
async function unsavedMemoryCardCount() {
  try {
    const cs = await invoke("cards.state", {});
    return (cs.count > 0 && !cs.root) ? cs.count : 0;
  } catch (_) { return 0; }
}

// Demande un chemin. Dans l'application réelle (pont natif présent), on
// ouvre le VRAI dialogue de fichiers de l'OS. Dans le navigateur (backend
// simulé), on se rabat sur une saisie manuelle du chemin.
// 'opts' (facultatif) : { kind, name, title } — kind vaut "markdown"
// (défaut), "epub", "pdf" ou "image" et choisit le filtre du dialogue
// natif ; name est le nom proposé ; title le titre de la fenêtre.
async function askPath(mode, opts) {
  const o = opts || {};
  if (hasNativeBridge) {
    const cmd = mode === "save" ? "file.chooseSave" : "file.chooseOpen";
    const payload = {};
    if (o.kind) payload.kind = o.kind;
    if (o.name) payload.name = o.name;
    if (o.title) payload.title = o.title;
    const r = await invoke(cmd, payload);
    return r.cancelled ? null : r.path;
  }
  // Repli navigateur : dialogue de saisie du chemin.
  document.getElementById("pathTitle").textContent =
    o.title || (mode === "save" ? "Enregistrer sous" : "Ouvrir un fichier");
  document.getElementById("pathInput").value = o.name || state.filePath || "";
  pathDialog.showModal();
  return new Promise((resolve) => { pathResolve = resolve; });
}
document.getElementById("pathCancel").addEventListener("click", () => {
  pathDialog.close();
  if (pathResolve) { pathResolve(null); pathResolve = null; }
});
document.getElementById("pathOk").addEventListener("click", () => {
  const path = document.getElementById("pathInput").value.trim();
  pathDialog.close();
  if (pathResolve) { pathResolve(path || null); pathResolve = null; }
});

async function doSaveFlow() {
  try {
    const path = await askPath("save");
    if (!path) return;
    state.filePath = path;
    document.getElementById("filePath").textContent = path;
    await persistTo(path, false); // enregistre + sauvegarde horodatée
    // Les fiches suivent le texte : on les écrit dans le dossier voisin.
    await attachCardsToManuscript(/*loadFromDisk=*/false);
  } catch (err) { reportError(err); }
}

async function doOpenFlow() {
  try {
    // Ouvrir un autre manuscrit charge SA bible et remplace les fiches en
    // mémoire : on prévient si certaines ne sont pas encore sur disque.
    const n = await unsavedMemoryCardCount();
    if (n > 0) {
      const ok = await askConfirm(
        n + (n > 1 ? " fiches ne sont pas encore enregistrées" : " fiche n'est pas encore enregistrée") +
        " sur disque. Ouvrir un autre manuscrit chargera sa bible narrative et remplacera ces fiches, qui seront perdues.",
        "Ouvrir quand même", "Fiches non enregistrées");
      if (!ok) return;
    }
    const path = await askPath("open");
    if (!path) return;
    await invoke("file.load", { path });
    const doc = await invoke("editor.getDocument", {});
    state.blocks = doc.blocks;
    state.activeIndex = 0;
    state.caret = { block: 0, pos: 0 };
    state.filePath = path;
    document.getElementById("filePath").textContent = path;
    renderAll();
    setCaret(0, 0);
    markClean(); // le document correspond au fichier qu'on vient d'ouvrir
    // Le dictionnaire personnel suit le manuscrit : on charge celui du
    // roman qu'on vient d'ouvrir, puis on revérifie tout le texte.
    clearSpellRanges();
    loadSpellDictionaries();
    // Charge la bible narrative rangée à côté du manuscrit, s'il y en a une.
    await attachCardsToManuscript(/*loadFromDisk=*/true);
    await updateManuscriptMentions(); // les fiches viennent d'être chargées
    setCaret(0, 0);
    refreshStatus();
    setStatus("Fichier ouvert.");
  } catch (err) { reportError(err); }
}

// Dossier des fiches associé à un manuscrit : « <nom>_fiches » à côté du
// fichier .md. Ainsi la bible narrative accompagne toujours le texte.
function cardsFolderFor(manuscriptPath) {
  if (!manuscriptPath) return "";
  const lastSep = Math.max(manuscriptPath.lastIndexOf("\\"), manuscriptPath.lastIndexOf("/"));
  const lastDot = manuscriptPath.lastIndexOf(".");
  const stem = (lastDot > lastSep) ? manuscriptPath.slice(0, lastDot) : manuscriptPath;
  return stem + "_fiches";
}

// Rattache les fiches au manuscrit courant. loadFromDisk=true : on lit le
// dossier voisin (ouverture) ; false : on y écrit les fiches en mémoire
// (enregistrement). Sans manuscrit enregistré, il n'y a pas de dossier :
// on ne fait rien (l'avertissement de la vue Fiches informe l'utilisateur).
async function attachCardsToManuscript(loadFromDisk) {
  const folder = cardsFolderFor(state.filePath);
  if (!folder) return;
  const r = await invoke(loadFromDisk ? "cards.open" : "cards.setRoot", { path: folder });
  cardsState.root = r.root || folder;
  updateCardsRoot();
  // Si la vue Fiches est ouverte, refléter le changement.
  if (!document.getElementById("cardsView").hidden) {
    cardsState.currentId = null;
    document.getElementById("cardFormBody").hidden = true;
    document.getElementById("cardFormEmpty").hidden = false;
    if (cardsState.schemasLoaded) await refreshCardList();
  }
}

document.getElementById("openBtn").addEventListener("click", doOpenFlow);
document.getElementById("saveBtn").addEventListener("click", doSaveFlow);

// =====================================================================
// 14bis. Paramètres : édition des raccourcis
// =====================================================================
const settingsDialog = document.getElementById("settingsDialog");
const shortcutListEl = document.getElementById("shortcutList");
const settingsMsgEl = document.getElementById("settingsMsg");

function setSettingsMsg(msg, isError) {
  settingsMsgEl.textContent = msg || "";
  settingsMsgEl.style.color = isError ? "#c0392b" : "var(--accent)";
}

function renderShortcutList() {
  shortcutListEl.textContent = "";
  for (const action of Object.keys(ACTIONS)) {
    const row = document.createElement("div");
    row.className = "shortcut-row";

    const label = document.createElement("span");
    label.className = "sc-label";
    label.textContent = ACTIONS[action].label;

    const combo = document.createElement("button");
    combo.className = "sc-combo";
    combo.textContent = recordingAction === action ? "Appuyez sur une combinaison…" : formatCombo(shortcuts[action]);
    if (recordingAction === action) combo.classList.add("recording");
    combo.addEventListener("mousedown", (e) => e.preventDefault());
    combo.addEventListener("click", () => startRecording(action));

    const clear = document.createElement("button");
    clear.className = "sc-clear";
    clear.textContent = "✕";
    clear.title = "Supprimer ce raccourci";
    clear.addEventListener("mousedown", (e) => e.preventDefault());
    clear.addEventListener("click", () => {
      shortcuts[action] = "";
      saveShortcuts();
      recordingAction = null;
      renderShortcutList();
      setSettingsMsg("Raccourci supprimé.");
    });

    row.append(label, combo, clear);
    shortcutListEl.appendChild(row);
  }
}

function startRecording(action) {
  recordingAction = action;
  setSettingsMsg("En attente d'une combinaison (Échap pour annuler)…");
  renderShortcutList();
}

// Capture prioritaire : quand on réattribue un raccourci, cet événement
// est consommé avant tous les autres gestionnaires.
document.addEventListener("keydown", (e) => {
  if (!recordingAction) return;
  e.preventDefault();
  e.stopPropagation();
  if (e.key === "Escape") { recordingAction = null; renderShortcutList(); setSettingsMsg(""); return; }
  const combo = comboFromEvent(e);
  if (!combo) return; // modificateur seul : on continue d'attendre
  if (!(e.ctrlKey || e.metaKey || e.altKey)) {
    setSettingsMsg("Utilisez au moins Ctrl ou Alt.", true);
    return;
  }
  // Vérification : le raccourci n'est pas déjà utilisé par une AUTRE action.
  const owner = Object.keys(shortcuts).find((a) => a !== recordingAction && shortcuts[a] === combo);
  if (owner) {
    setSettingsMsg("« " + formatCombo(combo) + " » est déjà utilisé par « " + ACTIONS[owner].label + " ».", true);
    return;
  }
  shortcuts[recordingAction] = combo;
  saveShortcuts();
  recordingAction = null;
  renderShortcutList();
  setSettingsMsg("Raccourci enregistré.");
}, true);

document.getElementById("settingsBtn").addEventListener("click", () => {
  recordingAction = null;
  setSettingsMsg("");
  renderShortcutList();
  renderAlertsPanel();
  // L'état du compte et de l'appairage se relit à l'ouverture : c'est une
  // lecture locale, elle ne coûte rien et évite d'afficher une
  // information périmée.
  refreshAccount();
  settingsDialog.showModal();
});
document.getElementById("settingsClose").addEventListener("click", () => { recordingAction = null; settingsDialog.close(); });
document.getElementById("shortcutsReset").addEventListener("click", () => {
  shortcuts = { ...DEFAULT_SHORTCUTS };
  saveShortcuts();
  recordingAction = null;
  renderShortcutList();
  setSettingsMsg("Raccourcis réinitialisés.");
});

// =====================================================================
// 14ter. Plan des chapitres + réorganisation par glisser-déposer
//        (cf. cadrage : « liste de chapitres, réorganisation par
//        glisser-déposer »). Un chapitre = un titre H1 et les blocs qui
//        le suivent jusqu'au H1 suivant.
// =====================================================================
const chapterPanel = document.getElementById("chapterPanel");
const chapterListEl = document.getElementById("chapterList");
let dragSourceChapter = null; // index (dans la liste) du chapitre déplacé
let lastChapterSig = "";

// Niveau d'un bloc titre : 1 à 6 pour h1..h6, 0 pour un paragraphe.
function headingLevel(type) {
  return (type && type.length === 2 && type[0] === "h") ? Number(type[1]) : 0;
}

// Construit le PLAN complet du manuscrit : une entrée par titre, tous
// niveaux confondus (h1 à h6), pour que les sous-parties apparaissent.
//
// Portée d'une entrée : du titre jusqu'au prochain titre de niveau
// INFÉRIEUR OU ÉGAL. Une sous-partie s'arrête donc au titre de même rang
// ou au chapitre suivant, et un chapitre englobe toutes ses sous-parties
// (c'est ce qui permet de déplacer un chapitre entier d'un seul geste).
//
// Les blocs situés avant le premier titre forment un « avant-propos »
// non déplaçable.
function computeOutline() {
  const blocks = state.blocks;
  const headings = [];
  for (let i = 0; i < blocks.length; i++) {
    const level = headingLevel(blocks[i].type);
    if (level >= 1 && level <= 6) headings.push({ index: i, level });
  }

  if (!headings.length) {
    // Aucun titre : un seul groupe implicite, rien à réorganiser.
    return [{ start: 0, end: blocks.length, level: 0,
              title: "(Manuscrit — aucun titre)", preamble: true }];
  }

  const entries = [];
  if (headings[0].index > 0) {
    entries.push({ start: 0, end: headings[0].index, level: 0,
                   title: "(Avant-propos)", preamble: true });
  }
  for (let k = 0; k < headings.length; k++) {
    const h = headings[k];
    let end = blocks.length;
    for (let j = k + 1; j < headings.length; j++) {
      if (headings[j].level <= h.level) { end = headings[j].index; break; }
    }
    entries.push({
      start: h.index, end, level: h.level,
      title: blocks[h.index].text.trim() || "(Sans titre)",
      preamble: false,
    });
  }
  return entries;
}

// Chapitres = entrées de premier niveau. Sert à la portée « chapitre
// courant » de la recherche et au compteur de mots par chapitre : on
// dérive du plan pour n'avoir qu'une seule logique de découpage.
function computeChapters() {
  return computeOutline().filter((e) => e.preamble || e.level === 1);
}

// Entrée à mettre en évidence : la PLUS PROFONDE contenant le curseur
// (une sous-partie plutôt que le chapitre qui l'englobe).
function activeChapterIndex(entries) {
  let best = -1;
  for (let i = 0; i < entries.length; i++) {
    if (state.activeIndex >= entries[i].start && state.activeIndex < entries[i].end) {
      if (best === -1 || entries[i].start >= entries[best].start) best = i;
    }
  }
  return best;
}

function renderChapters(force) {
  if (chapterPanel.hidden) return;
  if (dragSourceChapter !== null) return; // ne pas reconstruire pendant un glisser
  // Le plan liste TOUS les titres (h1 à h6), pas seulement les chapitres.
  const chapters = computeOutline();
  const activeIdx = activeChapterIndex(chapters);
  const sig = JSON.stringify(chapters) + "|" + activeIdx;
  if (!force && sig === lastChapterSig) return; // rien n'a changé
  lastChapterSig = sig;

  chapterListEl.textContent = "";
  chapters.forEach((chap, listIndex) => {
    const li = document.createElement("li");
    li.className = "chapter-item" + (chap.preamble ? " preamble" : "") + (listIndex === activeIdx ? " active" : "");
    if (chap.level > 1) li.classList.add("sub");
    // Indentation proportionnelle au niveau : la hiérarchie se lit d'un
    // coup d'oeil, et le nombre de mots de chaque section reste visible.
    li.style.paddingLeft = (0.55 + Math.max(0, chap.level - 1) * 0.9) + "rem";

    const label = document.createElement("span");
    label.className = "ch-title";
    label.textContent = chap.title;
    li.appendChild(label);

    if (!chap.preamble) {
      const badge = document.createElement("span");
      badge.className = "ch-level";
      badge.textContent = "H" + chap.level;
      li.appendChild(badge);
    }
    li.title = chap.preamble ? chap.title : ("H" + chap.level + " — " + chap.title);

    // Navigation : clic -> aller au premier bloc de la section.
    li.addEventListener("click", () => {
      state.activeIndex = chap.start;
      setCaret(chap.start, 0);
      const el = blockElAt(chap.start);
      if (el) el.scrollIntoView({ block: "start", behavior: "smooth" });
      breakGroupSoon();
    });

    if (!chap.preamble) {
      li.draggable = true;
      li.dataset.listIndex = String(listIndex);
      li.addEventListener("dragstart", (e) => {
        dragSourceChapter = listIndex;
        li.classList.add("dragging");
        e.dataTransfer.effectAllowed = "move";
        e.dataTransfer.setData("text/plain", String(listIndex)); // requis par Firefox
      });
      li.addEventListener("dragend", () => {
        dragSourceChapter = null;
        renderChapters(true);
      });
      li.addEventListener("dragover", (e) => {
        if (dragSourceChapter === null) return;
        e.preventDefault();
        e.dataTransfer.dropEffect = "move";
        const rect = li.getBoundingClientRect();
        const after = (e.clientY - rect.top) > rect.height / 2;
        li.classList.toggle("drop-after", after);
        li.classList.toggle("drop-before", !after);
      });
      li.addEventListener("dragleave", () => {
        li.classList.remove("drop-before", "drop-after");
      });
      li.addEventListener("drop", (e) => {
        e.preventDefault();
        const after = li.classList.contains("drop-after");
        li.classList.remove("drop-before", "drop-after");
        const source = chapters[dragSourceChapter];
        const target = chap;
        dragSourceChapter = null;
        if (!source) return;
        const before = after ? target.end : target.start;
        moveChapter(source.start, source.end - source.start, before);
      });
    }

    chapterListEl.appendChild(li);
  });
}

function moveChapter(start, count, before) {
  enqueue(async () => {
    const r = await invoke("editor.moveBlocks", { start, count, before });
    if (r.moved === false) return;
    applyWholeDocument(r);
    // Nouvelle position du premier bloc du chapitre déplacé (même calcul
    // que Document::moveBlocks côté C++).
    const newStart = (before > start) ? before - count : before;
    state.activeIndex = newStart;
    state.caret = { block: newStart, pos: 0 };
    setCaret(newStart, 0);
    const el = blockElAt(newStart);
    if (el) el.scrollIntoView({ block: "nearest" });
    renderChapters(true);
    refreshStatus();
    setStatus("Chapitre déplacé.");
  });
}

document.getElementById("chaptersToggle").addEventListener("click", () => {
  chapterPanel.hidden = !chapterPanel.hidden;
  if (!chapterPanel.hidden) renderChapters(true);
  try { localStorage.setItem("editeur.chapters", chapterPanel.hidden ? "0" : "1"); } catch (_) {}
});

// =====================================================================
// 14ter-bis. Analyse de cohérence (Phase 3)
//   Croise le manuscrit et les fiches pour signaler : liens cassés,
//   fiches jamais mentionnées (oublis possibles) et personnages décédés
//   encore cités (à vérifier). Purement consultatif : rien n'est modifié.
// =====================================================================
const coherenceDialog = document.getElementById("coherenceDialog");
document.getElementById("coherenceClose").addEventListener("click", () => coherenceDialog.close());
document.getElementById("coherenceBtn").addEventListener("click", runCoherenceReport);

async function runCoherenceReport() {
  try {
    // Les schémas servent à afficher les libellés de type (« Personnage »).
    if (!cardsState.schemas.length) {
      try { cardsState.schemas = (await invoke("cards.schemas", {})).schemas || []; } catch (_) {}
    }
    await refreshMentionIndex(); // titres de fiches à jour pour les rétroliens
    const backlinks = computeBacklinks();
    const all = ((await invoke("cards.list", {})).cards) || [];
    const broken = ((await invoke("cards.checkRefs", {})).broken) || [];

    // Orphelines : entités nommées (personnage, lieu, créature) jamais
    // citées dans le texte. Les autres types (langue, magie...) ne sont
    // pas censés apparaître par leur nom, on ne les signale pas.
    const named = new Set(["personnage", "lieu", "creature"]);
    const orphans = all.filter((c) => named.has(c.type) && !backlinks.has(c.id));

    // Ordre des chapitres, pour situer une mention par rapport au décès.
    const chapterOrder = new Map();
    mentionCtx.chapters.forEach((c, i) => { if (!chapterOrder.has(c.title)) chapterOrder.set(c.title, i); });

    const reappeared = []; // décès daté + mentions POSTÉRIEURES (incohérence)
    const toVerify = [];   // tagué « mort » sans chapitre de décès (à vérifier)
    for (const c of all) {
      const chaps = [...(backlinks.get(c.id) || [])];
      const hasDeathCh = c.deathChapter && chapterOrder.has(c.deathChapter);
      const tagged = (c.tags || []).some((t) => t === "mort" || t === "décédé" || t === "decede");
      if (hasDeathCh) {
        const dOrder = chapterOrder.get(c.deathChapter);
        const after = chaps.filter((t) => (chapterOrder.has(t) ? chapterOrder.get(t) : -1) > dOrder);
        if (after.length) reappeared.push({ card: c, chapters: after, deathChapter: c.deathChapter });
      } else if (tagged && chaps.length) {
        toVerify.push({ card: c, chapters: chaps });
      }
    }

    renderCoherence({ broken, orphans, reappeared, toVerify });
    coherenceDialog.showModal();
  } catch (err) { reportError(err); }
}

function coherenceSection(title, count, tone, hint, items) {
  const sec = document.createElement("div");
  sec.className = "coh-section";
  const h = document.createElement("h3");
  h.textContent = title + " ";
  const badge = document.createElement("span");
  badge.className = "coh-count " + tone;
  badge.textContent = count;
  h.appendChild(badge);
  sec.appendChild(h);
  if (hint) {
    const p = document.createElement("p");
    p.className = "coh-hint";
    p.textContent = hint;
    sec.appendChild(p);
  }
  if (!items.length) {
    const e = document.createElement("div");
    e.className = "coh-empty";
    e.textContent = "Rien à signaler.";
    sec.appendChild(e);
  } else {
    const ul = document.createElement("ul");
    ul.className = "coh-list";
    for (const it of items) ul.appendChild(it);
    sec.appendChild(ul);
  }
  return sec;
}

function coherenceItem(main, sub, cardId) {
  const li = document.createElement("li");
  li.className = "coh-item" + (cardId ? " clickable" : "");
  const m = document.createElement("div");
  m.textContent = main;
  li.appendChild(m);
  if (sub) {
    const s = document.createElement("div");
    s.className = "coh-sub";
    s.textContent = sub;
    li.appendChild(s);
  }
  if (cardId) {
    li.addEventListener("click", () => { coherenceDialog.close(); openCardFromManuscript(cardId); });
  }
  return li;
}

function renderCoherence({ broken, orphans, reappeared, toVerify }) {
  const body = document.getElementById("coherenceBody");
  body.textContent = "";

  // 1. Références cassées.
  body.appendChild(coherenceSection(
    "Liens cassés", broken.length, broken.length ? "bad" : "ok",
    "Une relation d'une fiche pointe vers une fiche qui n'existe pas (ou plus).",
    broken.map((b) => coherenceItem(
      (b.title || b.id) + " — " + (b.fieldLabel || b.field) + " → « " + b.targetId + " »",
      "Fiche cible introuvable", b.id))));

  // 2. Fiches orphelines.
  body.appendChild(coherenceSection(
    "Fiches jamais mentionnées", orphans.length, orphans.length ? "warn" : "ok",
    "Personnages, lieux ou créatures dont le nom n'apparaît nulle part dans le texte — oubli possible.",
    orphans.map((c) => coherenceItem(c.title || c.id, labelOfType(c.type), c.id))));

  // 3. Personnages morts qui RÉAPPARAISSENT après leur chapitre de décès.
  body.appendChild(coherenceSection(
    "Personnages morts réapparus", reappeared.length, reappeared.length ? "bad" : "ok",
    "Fiches ayant un « Décès (chapitre) » : ces personnages sont mentionnés APRÈS le chapitre de leur mort (mentions en rouge dans le texte).",
    reappeared.map((d) => coherenceItem(
      d.card.title || d.card.id,
      "Mort au « " + d.deathChapter + " », réapparaît dans : " + d.chapters.join(", "), d.card.id))));

  // 4. Personnages taggués « mort » sans chapitre de décès (à vérifier).
  body.appendChild(coherenceSection(
    "Décès à vérifier", toVerify.length, toVerify.length ? "warn" : "ok",
    "Fiches taguées « mort » sans « Décès (chapitre) » renseigné : précisez le chapitre de décès pour détecter automatiquement les réapparitions.",
    toVerify.map((d) => coherenceItem(
      d.card.title || d.card.id,
      "Mentionné dans : " + d.chapters.join(", "), d.card.id))));
}

// =====================================================================
// 14quater. Recherche / remplacement (Ctrl+F)
//           La portée (chapitre courant / manuscrit entier) est traduite
//           en plage de blocs ; le moteur de recherche est côté C++.
// =====================================================================
const findBar = document.getElementById("findBar");
const findInput = document.getElementById("findInput");
const replaceInput = document.getElementById("replaceInput");
const findCount = document.getElementById("findCount");
const findCaseBtn = document.getElementById("findCase");
const findWordBtn = document.getElementById("findWord");
const findScopeSel = document.getElementById("findScope");
let findMatch = null;   // { block, start, end } occurrence courante
let findQuery = "";     // requête ayant produit findMatch

function findOptions() {
  return {
    caseSensitive: findCaseBtn.classList.contains("on"),
    wholeWord: findWordBtn.classList.contains("on"),
  };
}

// Plage de blocs [rangeStart, rangeEnd) selon la portée choisie.
function scopeRange() {
  // « Chapitre courant » restreint la plage ; « Manuscrit entier » et
  // « Manuscrit + fiches » couvrent tout le texte (les fiches sont
  // cherchées à part, cf. runFind).
  if (findScopeSel.value === "chapter") {
    const chapters = computeChapters();
    for (const c of chapters) {
      if (state.activeIndex >= c.start && state.activeIndex < c.end) return { rangeStart: c.start, rangeEnd: c.end };
    }
  }
  return { rangeStart: 0, rangeEnd: state.blocks.length };
}
function scopeIncludesCards() { return findScopeSel.value === "all"; }

function openFindBar() {
  findBar.hidden = false;
  // Pré-remplit avec la sélection courante si elle tient dans un bloc.
  const el = blockElAt(state.activeIndex);
  if (el) {
    const sel = selectionInBlock(el);
    if (sel && !sel.collapsed) {
      const cps = cpArray(state.blocks[state.activeIndex].text);
      findInput.value = cps.slice(sel.start, sel.end).join("");
    }
  }
  findInput.focus();
  findInput.select();
  runFind(false, true);
}

function closeFindBar() {
  findBar.hidden = true;
  document.getElementById("cardResults").hidden = true;
  clearFindHighlight();
  findMatch = null;
  focusActive();
}

function clearFindHighlight() {
  if (window.CSS && CSS.highlights) CSS.highlights.delete("find-current");
}

// Surligne l'occurrence (API CSS Custom Highlight ; repli : sélection).
function highlightMatch(block, start, end) {
  const el = blockElAt(block);
  if (!el) return;
  const s = locateCp(el, start), e = locateCp(el, end);
  const range = document.createRange();
  range.setStart(s.node, s.offset);
  range.setEnd(e.node, e.offset);
  if (window.CSS && CSS.highlights && window.Highlight) {
    CSS.highlights.set("find-current", new Highlight(range));
  } else {
    const sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
  }
  el.scrollIntoView({ block: "nearest" });
}

// Lance une recherche. 'fresh' = nouvelle requête (repart du curseur) ;
// sinon on continue à partir de l'occurrence courante.
function runFind(backward, fresh) {
  const query = findInput.value;
  if (!query) { findCount.textContent = "—"; findCount.classList.remove("none"); clearFindHighlight(); findMatch = null; return; }
  const scope = scopeRange();
  let fromBlock, fromPos;
  if (!fresh && findMatch && findQuery === query) {
    fromBlock = findMatch.block;
    fromPos = backward ? findMatch.start : findMatch.end;
  } else {
    fromBlock = state.activeIndex;
    fromPos = state.caret.pos;
  }
  findQuery = query;
  enqueue(async () => {
    const r = await invoke("editor.find", Object.assign({
      query, fromBlock, fromPos, backward, wrap: true,
    }, scope, findOptions()));
    if (!r.found) {
      findMatch = null;
      findCount.textContent = r.total > 0 ? "0 / " + r.total : "Texte : aucun";
      findCount.classList.toggle("none", r.total === 0);
      clearFindHighlight();
    } else {
      findMatch = { block: r.block, start: r.start, end: r.end };
      state.activeIndex = r.block;
      findCount.textContent = r.index + " / " + r.total;
      findCount.classList.remove("none");
      highlightMatch(r.block, r.start, r.end);
    }
    // Portée « Manuscrit + fiches » : chercher aussi dans les fiches.
    await runCardSearch(query);
  });
}

// Recherche dans les fiches (portée « all »), affichée sous la barre.
async function runCardSearch(query) {
  const box = document.getElementById("cardResults");
  if (!scopeIncludesCards() || !query) { box.hidden = true; return; }
  try {
    const r = await invoke("cards.search", Object.assign({ query }, findOptions()));
    renderCardResults(r.results || []);
  } catch (_) { box.hidden = true; }
}

function renderCardResults(results) {
  const box = document.getElementById("cardResults");
  const head = document.getElementById("cardResultsHead");
  const list = document.getElementById("cardResultsList");
  box.hidden = false;
  list.textContent = "";
  head.textContent = "Fiches : " + results.length + (results.length > 1 ? " résultats" : " résultat");
  for (const res of results) {
    const li = document.createElement("li");
    li.className = "card-result";

    const title = document.createElement("div");
    title.className = "cr-title";
    title.textContent = res.title || "(sans titre)";

    const field = document.createElement("div");
    field.className = "cr-field";
    field.textContent = labelOfType(res.type) + " · " + (res.fieldLabel || res.field);

    const snip = document.createElement("div");
    snip.className = "cr-snippet";
    snip.textContent = res.snippet || "";

    li.append(title, field, snip);
    li.addEventListener("click", () => openCardFromManuscript(res.id));
    list.appendChild(li);
  }
}

async function replaceCurrent() {
  if (!findMatch) { runFind(false, false); return; }
  const m = findMatch;
  const replacement = replaceInput.value;
  await enqueue(async () => {
    const r = await invoke("editor.replaceRange", {
      block: m.block, start: m.start, count: m.end - m.start, text: replacement,
    });
    applyBlockResult(m.block, r);
    markDirty();
    // Repart juste après le texte inséré pour trouver l'occurrence suivante.
    findMatch = { block: m.block, start: r.caretPos, end: r.caretPos };
  });
  runFind(false, false);
  refreshStatus();
}

async function replaceAllInScope() {
  const query = findInput.value;
  if (!query) return;
  const replacement = replaceInput.value;
  const scope = scopeRange();
  const alsoCards = scopeIncludesCards();

  // Remplacer dans les fiches modifie plusieurs fichiers d'un coup : on
  // demande confirmation avant.
  if (alsoCards) {
    const ok = await askConfirm(
      "Remplacer « " + query + " » par « " + replacement + " » dans le manuscrit " +
      "ET dans toutes les fiches ? Cette action n'est pas annulable pour les fiches.",
      "Tout remplacer", "Remplacement global");
    if (!ok) return;
  }

  enqueue(async () => {
    const r = await invoke("editor.replaceAll", Object.assign({
      query, replacement,
    }, scope, findOptions()));
    applyWholeDocument(r);
    markDirty();
    findMatch = null;
    clearFindHighlight();

    let cardCount = 0;
    if (alsoCards) {
      const rc = await invoke("cards.replaceAll", Object.assign({ query, replacement }, findOptions()));
      cardCount = rc.count || 0;
      await runCardSearch(query); // rafraîchit la liste (devrait se vider)
      await updateManuscriptMentions(); // des titres de fiches ont pu changer
    }

    const total = r.count + cardCount;
    findCount.textContent = total + (total > 1 ? " remplacés" : " remplacé");
    findCount.classList.remove("none");
    refreshStatus();
    setStatus(r.count + " dans le texte" + (alsoCards ? ", " + cardCount + " dans les fiches" : "") + ".");
  });
}

findInput.addEventListener("input", () => runFind(false, true));
findInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") { e.preventDefault(); runFind(e.shiftKey, false); }
  else if (e.key === "Escape") { e.preventDefault(); closeFindBar(); }
});
replaceInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") { e.preventDefault(); replaceCurrent(); }
  else if (e.key === "Escape") { e.preventDefault(); closeFindBar(); }
});
document.getElementById("findNext").addEventListener("click", () => runFind(false, false));
document.getElementById("findPrev").addEventListener("click", () => runFind(true, false));
document.getElementById("findClose").addEventListener("click", closeFindBar);
document.getElementById("replaceOne").addEventListener("click", replaceCurrent);
document.getElementById("replaceAll").addEventListener("click", replaceAllInScope);
[findCaseBtn, findWordBtn].forEach((b) => b.addEventListener("click", () => {
  b.classList.toggle("on");
  runFind(false, true);
}));
findScopeSel.addEventListener("change", () => runFind(false, true));

// =====================================================================
// 14quinquies. Sauvegarde automatique + backups horodatés
//              (cf. cadrage). L'écriture atomique et les backups sont
//              côté C++ (io) ; ici on gère la cadence et l'état « modifié ».
// =====================================================================
const AUTOSAVE_INTERVAL_MS = 30000; // 30 s
const BACKUP_KEEP = 10;

function markDirty() {
  if (!state.dirty) { state.dirty = true; pushStateToNative(); } // seulement au changement
  updateSaveState();
}
function markClean() {
  state.dirty = false;
  pushStateToNative();
  updateSaveState();
}

// Tient le C++ informé : c'est lui qui garde la fenêtre et doit prévenir
// l'utilisateur s'il ferme avec des modifications non enregistrées.
function pushStateToNative() {
  invoke("file.setState", { dirty: state.dirty, path: state.filePath }).catch(() => {});
}
function updateSaveState() {
  const el = document.getElementById("saveState");
  if (!state.filePath && !state.dirty) { el.textContent = ""; return; }
  el.textContent = state.dirty ? "● Modifié" : "✓ Enregistré";
  el.style.color = state.dirty ? "var(--accent)" : "var(--muted)";
}

// Enregistre le fichier de travail ET écrit une sauvegarde horodatée.
// Utilisé par la sauvegarde manuelle comme automatique.
async function persistTo(path, auto) {
  await drain();
  let r = await invoke("file.save", { path });
  if (r && r.conflict) {
    // Le fichier a changé en dehors de l'éditeur depuis son ouverture.
    if (auto) {
      // La sauvegarde AUTOMATIQUE ne doit jamais écraser en silence : on
      // la reporte et on prévient. L'utilisateur tranchera à la main.
      markDirty();
      setStatus("Sauvegarde auto suspendue : le fichier a été modifié en dehors de l'éditeur.");
      return;
    }
    // Choix à trois branches : écraser, renoncer, ou VOIR ce qui diffère.
    // « Écraser ou renoncer » est un choix aveugle tant qu'on ignore ce
    // que contient le fichier sur le disque.
    const choix = await askConfirm(
      "Le fichier a été modifié en dehors de l'éditeur (peut-être une autre fenêtre) depuis " +
      "son ouverture. Écraser ces modifications extérieures ?",
      "Écraser", "Conflit de fichier", "Voir les différences");
    if (choix === "third") {
      // On n'enregistre pas : l'auteur regarde, puis relance la
      // sauvegarde en connaissance de cause.
      await openCompareWith(path);
      setStatus("Comparaison affichée. Relancez l'enregistrement une fois décidé.");
      return;
    }
    if (!choix) return;
    r = await invoke("file.save", { path, force: true });
  }
  try { await invoke("file.backup", { path, keep: BACKUP_KEEP }); } catch (_) { /* backup best-effort */ }
  // Suivi du projet : on note ce qui a changé (inventaire + journal).
  // Au pire des cas c'est un confort en moins, jamais une sauvegarde
  // perdue : l'échec est donc silencieux, comme pour la sauvegarde
  // horodatée juste au-dessus.
  try { await invoke("project.snapshot", { path }); } catch (_) { /* suivi best-effort */ }
  markClean();
  const now = new Date().toLocaleTimeString("fr-FR", { hour: "2-digit", minute: "2-digit" });
  setStatus(auto ? "Sauvegarde automatique à " + now + "." : "Enregistré à " + now + ".");
}

setInterval(() => {
  if (state.dirty && state.filePath) {
    persistTo(state.filePath, true).catch(reportError);
  }
}, AUTOSAVE_INTERVAL_MS);

// =====================================================================
// 14sexies. Alertes de session et rappels d'échéance (paramétrables)
//           cf. cadrage : « alertes paramétrables (fin de session
//           d'écriture ou de pause) » et « rappels paramétrables selon
//           la date (exemple : rendu) ».
// =====================================================================
const DEFAULT_ALERTS = {
  session: { enabled: false, workMin: 45, breakMin: 5 },
  reminders: [], // { id, date: "AAAA-MM-JJ", label, done }
};
let alerts = loadAlerts();
let sessionPhase = "work";       // "work" | "break"
let sessionStartedAt = Date.now();
const alertQueue = [];
let alertShowing = false;

function loadAlerts() {
  const base = JSON.parse(JSON.stringify(DEFAULT_ALERTS));
  try {
    const saved = JSON.parse(localStorage.getItem("editeur.alerts") || "{}");
    if (saved.session) Object.assign(base.session, saved.session);
    if (Array.isArray(saved.reminders)) base.reminders = saved.reminders;
  } catch (_) {}
  return base;
}
function saveAlerts() {
  try { localStorage.setItem("editeur.alerts", JSON.stringify(alerts)); } catch (_) {}
}

// --- Bannière (non bloquante : on n'interrompt jamais la frappe) ---
const alertBanner = document.getElementById("alertBanner");
function showAlert(text, icon) {
  alertQueue.push({ text, icon: icon || "⏰" });
  if (!alertShowing) nextAlert();
}
function nextAlert() {
  const a = alertQueue.shift();
  if (!a) { alertShowing = false; alertBanner.hidden = true; return; }
  alertShowing = true;
  document.getElementById("alertIcon").textContent = a.icon;
  document.getElementById("alertText").textContent = a.text;
  alertBanner.hidden = false;
}
document.getElementById("alertDismiss").addEventListener("click", nextAlert);

// --- Session d'écriture / pause ---
function restartSession() {
  sessionPhase = "work";
  sessionStartedAt = Date.now();
}
function sessionRemainingMin() {
  if (!alerts.session.enabled) return null;
  const limit = (sessionPhase === "work" ? alerts.session.workMin : alerts.session.breakMin) * 60000;
  return Math.max(0, Math.ceil((limit - (Date.now() - sessionStartedAt)) / 60000));
}
function checkSession() {
  if (!alerts.session.enabled) return;
  const limit = (sessionPhase === "work" ? alerts.session.workMin : alerts.session.breakMin) * 60000;
  if (Date.now() - sessionStartedAt < limit) return;
  if (sessionPhase === "work") {
    showAlert("Fin de la session d'écriture — pensez à faire une pause de " + alerts.session.breakMin + " min.", "☕");
    sessionPhase = "break";
  } else {
    showAlert("Fin de la pause — bonne reprise !", "✍️");
    sessionPhase = "work";
  }
  sessionStartedAt = Date.now();
}

// --- Rappels par date ---
function todayISO() {
  const d = new Date();
  const p = (n) => String(n).padStart(2, "0");
  return d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate());
}
function daysUntil(dateISO) {
  const today = new Date(todayISO() + "T00:00:00");
  const due = new Date(dateISO + "T00:00:00");
  return Math.round((due - today) / 86400000);
}
function checkReminders() {
  const today = todayISO();
  let changed = false;
  for (const r of alerts.reminders) {
    if (r.done || r.notified) continue;
    if (r.date <= today) {
      const late = daysUntil(r.date) < 0;
      showAlert("Rappel : " + r.label + (late ? " (échéance dépassée du " + r.date + ")" : " — c'est aujourd'hui."), "📌");
      r.notified = true; // ne pas re-notifier à chaque vérification
      changed = true;
    }
  }
  if (changed) saveAlerts();
}

// Vérification périodique (session + rappels) et mise à jour de l'état.
setInterval(() => { checkSession(); checkReminders(); updateSessionIndicator(); }, 15000);

function updateSessionIndicator() {
  const el = document.getElementById("sessionState");
  const remaining = sessionRemainingMin();
  if (remaining === null) { el.textContent = ""; return; }
  el.textContent = (sessionPhase === "work" ? "✍️ " : "☕ ") + remaining + " min";
}

// --- Panneau de paramétrage ---
function renderReminders() {
  const list = document.getElementById("reminderList");
  list.textContent = "";
  const sorted = alerts.reminders.slice().sort((a, b) => a.date.localeCompare(b.date));
  if (!sorted.length) {
    const empty = document.createElement("div");
    empty.className = "hint";
    empty.textContent = "Aucun rappel.";
    list.appendChild(empty);
    return;
  }
  for (const r of sorted) {
    const row = document.createElement("div");
    row.className = "reminder-row";

    const date = document.createElement("span");
    date.className = "rem-date";
    date.textContent = r.date;

    const label = document.createElement("span");
    label.className = "rem-label";
    label.textContent = r.label;

    const due = document.createElement("span");
    const d = daysUntil(r.date);
    due.className = "rem-due";
    due.textContent = d < 0 ? "dépassé" : (d === 0 ? "aujourd'hui" : "dans " + d + " j");

    const del = document.createElement("button");
    del.className = "rem-del";
    del.textContent = "✕";
    del.title = "Supprimer ce rappel";
    del.addEventListener("click", () => {
      alerts.reminders = alerts.reminders.filter((x) => x.id !== r.id);
      saveAlerts();
      renderReminders();
    });

    row.append(date, label, due, del);
    list.appendChild(row);
  }
}

function renderAlertsPanel() {
  document.getElementById("sessionEnabled").checked = alerts.session.enabled;
  document.getElementById("sessionWork").value = alerts.session.workMin;
  document.getElementById("sessionBreak").value = alerts.session.breakMin;
  renderReminders();
}

function readAlertsPanel() {
  const work = Number(document.getElementById("sessionWork").value) || 45;
  const brk = Number(document.getElementById("sessionBreak").value) || 5;
  alerts.session.enabled = document.getElementById("sessionEnabled").checked;
  alerts.session.workMin = Math.min(240, Math.max(1, work));
  alerts.session.breakMin = Math.min(120, Math.max(1, brk));
  saveAlerts();
  restartSession(); // tout changement relance le décompte
  updateSessionIndicator();
}
["sessionEnabled", "sessionWork", "sessionBreak"].forEach((id) => {
  document.getElementById(id).addEventListener("change", readAlertsPanel);
});

document.getElementById("reminderAdd").addEventListener("click", () => {
  const date = document.getElementById("reminderDate").value;
  const label = document.getElementById("reminderLabel").value.trim();
  if (!date || !label) { setSettingsMsg("Indiquez une date et un intitulé.", true); return; }
  alerts.reminders.push({ id: Date.now() + "-" + Math.random().toString(16).slice(2), date, label, done: false });
  saveAlerts();
  document.getElementById("reminderLabel").value = "";
  renderReminders();
  setSettingsMsg("Rappel ajouté.");
});

// Onglets du dialogue Paramètres (Apparence / Raccourcis / Alertes).
document.querySelectorAll(".tabs .tab").forEach((tab) => {
  tab.addEventListener("click", () => {
    document.querySelectorAll(".tabs .tab").forEach((t) => t.classList.toggle("on", t === tab));
    document.querySelectorAll(".tab-panel").forEach((p) => {
      p.hidden = p.id !== ("tab-" + tab.dataset.tab);
    });
    // « Réinitialiser les raccourcis » n'a de sens que sur son onglet.
    document.getElementById("shortcutsReset").hidden = tab.dataset.tab !== "shortcuts";
    setSettingsMsg("");
  });
});
// État initial : l'onglet Apparence est actif au premier affichage.
document.getElementById("shortcutsReset").hidden = true;

// =====================================================================
// 14sexies. Compte et appairage (Phase 5, jalon C)
//
//   RIEN ICI NE PART SUR LE RÉSEAU tant que l'auteur ne clique pas sur
//   « Se connecter ». account.state, qui décrit l'état, est une commande
//   purement locale : elle lit un fichier à côté du manuscrit et rien
//   d'autre. Côté C++, aucune session réseau n'est même construite avant
//   la connexion (voir net/Connection.h).
//
//   Le mot de passe est effacé du champ dès l'envoi : il n'a aucune
//   raison de rester dans le document après avoir servi.
// =====================================================================
const account = {
  connected: false,
  pairing: { state: "sans-fichier", canPropose: false, autoSync: false },
};

// Réglage global : proposer l'appairage à la connexion. Activé par
// défaut, désactivable — au même titre que la synchronisation
// automatique, qui elle est désactivée par défaut (côté projet).
function proposePairingEnabled() {
  try { return localStorage.getItem("editeur.proposerAppairage") !== "non"; }
  catch (_) { return true; }
}
function setProposePairing(enabled) {
  try { localStorage.setItem("editeur.proposerAppairage", enabled ? "oui" : "non"); }
  catch (_) {}
}

function renderAccount() {
  document.getElementById("accountOut").hidden = account.connected;
  document.getElementById("accountIn").hidden = !account.connected;
  if (account.connected) {
    document.getElementById("accountWho").textContent =
      "Connecté en tant que « " + (account.user || "") + " » sur " + (account.server || "") + ".";
  }

  const p = account.pairing || {};
  const libelle = {
    "sans-fichier": "Enregistrez d'abord le manuscrit : l'appairage se range à côté du fichier.",
    "aucun": "Ce projet n'est pas appairé. Rien n'en part.",
    "appaire": "Ce projet est appairé à " + (p.server || "") + " ("
      + (p.mode === "deux-sens" ? "deux sens" : "lecture seule") + ").",
    "jamais": "Vous avez demandé à ne plus être sollicité pour ce projet.",
  }[p.state] || "—";
  document.getElementById("accountPairing").textContent = libelle;

  // On ne propose d'appairer que si c'est possible : connecté, manuscrit
  // enregistré, et décision pas encore prise.
  document.getElementById("accountPairRow").hidden =
    !(account.connected && p.canPropose);
  document.getElementById("accountUnpairRow").hidden = (p.state !== "appaire");
  document.getElementById("accountAutoSync").checked = !!p.autoSync;
  document.getElementById("accountPropose").checked = proposePairingEnabled();
}

async function refreshAccount() {
  try {
    const r = await invoke("account.state", { path: state.filePath || "" });
    account.connected = r.connected;
    account.server = r.server;
    account.user = r.user;
    account.pairing = r.pairing;
    renderAccount();
  } catch (err) { reportError(err); }
}

// La proposition d'appairage, après une connexion réussie. Trois
// réponses, et « jamais » est retenu pour de bon : une question reposée
// en boucle finit par être acceptée par lassitude, ce qui ne serait pas
// un consentement.
async function proposePairing() {
  // Sans fichier sur le disque, il n'y a nulle part où ranger la
  // décision : on ne pose pas une question dont la réponse serait perdue.
  if (!state.filePath) return;
  const nom = state.filePath.replace(/^.*[\\/]/, "");
  const reponse = await askConfirm(
    "Synchroniser « " + nom + " » avec " + (account.server || "le serveur") + " ? "
    + "Le fichier sur votre disque reste la référence ; le serveur n'en sera "
    + "qu'un miroir, en lecture seule pour l'instant.",
    "Appairer", "Synchroniser ce projet ?", "Jamais pour ce projet");
  try {
    if (reponse === true) {
      const r = await invoke("account.pair",
                             { path: state.filePath, mode: "lecture-seule" });
      account.pairing = r.pairing;
      setStatus("Projet appairé.");
    } else if (reponse === "third") {
      const r = await invoke("account.never", { path: state.filePath });
      account.pairing = r.pairing;
      setStatus("Ce projet ne vous sera plus proposé.");
    } else {
      setStatus("Appairage remis à plus tard.");
    }
    renderAccount();
  } catch (err) { reportError(err); }
}

document.getElementById("accountSignIn").addEventListener("click", async () => {
  const champSecret = document.getElementById("accountSecret");
  const secret = champSecret.value;
  const server = document.getElementById("accountServer").value.trim();
  const user = document.getElementById("accountUser").value.trim();
  // Effacé AVANT même de savoir si la connexion réussit : le champ n'a
  // plus rien à en faire.
  champSecret.value = "";

  if (!server || !user || !secret) {
    setSettingsMsg("Serveur, identifiant et mot de passe sont nécessaires.", true);
    return;
  }
  try {
    setSettingsMsg("Connexion…");
    const r = await invoke("account.signIn",
                           { server: server, user: user, secret: secret,
                             path: state.filePath || "" });
    if (!r.connected) {
      setSettingsMsg(r.message || "Connexion refusée.", true);
      return;
    }
    account.connected = true;
    account.server = r.server;
    account.user = r.user;
    account.pairing = r.pairing;
    renderAccount();
    setSettingsMsg("Connecté.");
    if (proposePairingEnabled() && account.pairing && account.pairing.canPropose) {
      await proposePairing();
    }
  } catch (err) {
    setSettingsMsg(String(err && err.message ? err.message : err), true);
  }
});

document.getElementById("accountSignOut").addEventListener("click", async () => {
  try {
    await invoke("account.signOut", {});
    account.connected = false;
    account.user = "";
    account.server = "";
    renderAccount();
    setSettingsMsg("Déconnecté.");
  } catch (err) { setSettingsMsg(String(err), true); }
});

async function pairingAction(commande, message, charge) {
  try {
    const r = await invoke(commande, Object.assign({ path: state.filePath || "" }, charge || {}));
    account.pairing = r.pairing;
    renderAccount();
    setSettingsMsg(message);
  } catch (err) { setSettingsMsg(String(err && err.message ? err.message : err), true); }
}

document.getElementById("accountPair").addEventListener("click",
  () => pairingAction("account.pair", "Projet appairé.", { mode: "lecture-seule" }));
document.getElementById("accountNever").addEventListener("click",
  () => pairingAction("account.never", "Ce projet ne vous sera plus proposé."));
document.getElementById("accountUnpair").addEventListener("click",
  () => pairingAction("account.unpair", "Projet désappairé."));
document.getElementById("accountAutoSync").addEventListener("change", (e) =>
  pairingAction("account.setAutoSync",
                e.target.checked ? "Synchronisation automatique activée."
                                 : "Synchronisation automatique désactivée.",
                { enabled: e.target.checked }));
document.getElementById("accountPropose").addEventListener("change", (e) => {
  setProposePairing(e.target.checked);
  setSettingsMsg(e.target.checked ? "L'appairage vous sera proposé à la connexion."
                                  : "L'appairage ne vous sera plus proposé.");
});

// =====================================================================
// 14septies. Fiches de worldbuilding (Phase 2)
//
//   Les formulaires sont ENTIÈREMENT construits à partir des schémas
//   renvoyés par cards.schemas. Ajouter un type de fiche côté C++ le
//   fait apparaître ici avec son formulaire, sans toucher à ce code :
//   c'est l'exigence d'extensibilité du cadrage, rendue concrète.
// =====================================================================
const cardsState = {
  schemas: [],      // descriptifs des types, venus du C++
  list: [],         // résumés des fiches affichées
  currentId: null,  // fiche en cours d'édition (null = nouvelle fiche)
  root: "",
  schemasLoaded: false, // schémas récupérés du C++ ?
};
let cardFieldReaders = [];   // [{ key, read() }] pour relire le formulaire
let cardTagsReader = null;

// --- Bascule manuscrit / fiches --------------------------------------
function setMode(mode) {
  const cards = (mode === "fiches");
  document.getElementById("workspace").hidden = cards;
  document.getElementById("cardsView").hidden = !cards;
  document.querySelectorAll(".editor-only").forEach((el) => { el.hidden = cards; });
  document.getElementById("modeToggle").textContent = cards ? "📄 Manuscrit" : "📔 Fiches";
  if (cards) {
    findBar.hidden = true;
    initCardsView();
  } else {
    // Retour au manuscrit : les fiches ont pu changer, on rafraîchit les
    // mentions (met aussi le manuscrit à jour visuellement).
    updateManuscriptMentions();
    focusActive();
  }
}
document.getElementById("modeToggle").addEventListener("click", () => {
  setMode(document.getElementById("cardsView").hidden ? "fiches" : "manuscrit");
});

async function initCardsView() {
  if (cardsState.schemasLoaded) { updateCardsRoot(); return; }
  try {
    const r = await invoke("cards.schemas", {});
    cardsState.schemas = r.schemas || [];
    const st = await invoke("cards.state", {});
    cardsState.root = st.root || "";
    updateCardsRoot();
    fillTypeFilter();
    cardsState.schemasLoaded = true;
    await refreshCardList();
  } catch (err) { reportError(err); }
}

function updateCardsRoot() {
  const el = document.getElementById("cardsRootPath");
  el.textContent = cardsState.root || "Non enregistrées sur disque";
  el.title = cardsState.root;
  // Avertissement visible tant qu'aucun dossier n'est associé.
  const warn = document.getElementById("cardsWarning");
  if (warn) warn.hidden = !!cardsState.root;
}

function fillTypeFilter() {
  const sel = document.getElementById("cardTypeFilter");
  const current = sel.value;
  sel.textContent = "";
  const all = document.createElement("option");
  all.value = ""; all.textContent = "Tous les types";
  sel.appendChild(all);
  for (const schema of cardsState.schemas) {
    const opt = document.createElement("option");
    opt.value = schema.typeId;
    opt.textContent = schema.label;
    sel.appendChild(opt);
  }
  sel.value = current;
}

// --- Liste des fiches -------------------------------------------------
async function refreshCardList() {
  try {
    const payload = {};
    const type = document.getElementById("cardTypeFilter").value;
    const tag = document.getElementById("cardTagFilter").value;
    const query = document.getElementById("cardSearch").value.trim();
    if (type) payload.type = type;
    if (tag) payload.tag = tag;
    if (query) payload.query = query;

    const r = await invoke("cards.list", payload);
    cardsState.list = r.cards || [];
    renderCardList();

    const t = await invoke("cards.tags", {});
    fillTagFilter(t.tags || []);
  } catch (err) { reportError(err); }
}

function fillTagFilter(tags) {
  const sel = document.getElementById("cardTagFilter");
  const current = sel.value;
  sel.textContent = "";
  const all = document.createElement("option");
  all.value = ""; all.textContent = "Tous les tags";
  sel.appendChild(all);
  for (const tag of tags) {
    const opt = document.createElement("option");
    opt.value = tag; opt.textContent = tag;
    sel.appendChild(opt);
  }
  sel.value = tags.includes(current) ? current : "";
}

function labelOfType(typeId) {
  const schema = cardsState.schemas.find((s) => s.typeId === typeId);
  return schema ? schema.label : (typeId || "—");
}

function renderCardList() {
  const list = document.getElementById("cardItems");
  list.textContent = "";
  if (!cardsState.list.length) {
    const empty = document.createElement("li");
    empty.className = "card-item muted";
    empty.textContent = "Aucune fiche.";
    list.appendChild(empty);
    return;
  }
  for (const card of cardsState.list) {
    const li = document.createElement("li");
    li.className = "card-item" + (card.id === cardsState.currentId ? " active" : "");

    const title = document.createElement("span");
    title.className = "ci-title";
    title.textContent = card.title || "(sans titre)";

    const meta = document.createElement("span");
    meta.className = "ci-meta";
    meta.textContent = labelOfType(card.type) + (card.tags && card.tags.length ? " · " + card.tags.join(", ") : "");

    li.append(title, meta);
    li.addEventListener("click", () => selectCard(card.id));
    list.appendChild(li);
  }
}

// --- Contrôles de saisie, choisis d'après le TYPE du champ ------------
function buildListControl(values, placeholder) {
  const container = document.createElement("div");
  const inputs = [];
  const addBtn = document.createElement("button");
  addBtn.type = "button";
  addBtn.className = "list-add";
  addBtn.textContent = "+ Ajouter";
  container.appendChild(addBtn);

  const addRow = (value) => {
    const row = document.createElement("div");
    row.className = "list-row";
    const input = document.createElement("input");
    input.type = "text";
    input.value = value || "";
    if (placeholder) input.placeholder = placeholder;
    const del = document.createElement("button");
    del.type = "button";
    del.textContent = "✕";
    del.title = "Retirer";
    del.addEventListener("click", () => {
      container.removeChild(row);
      const i = inputs.indexOf(input);
      if (i >= 0) inputs.splice(i, 1);
    });
    row.append(input, del);
    container.insertBefore(row, addBtn);
    inputs.push(input);
  };
  addBtn.addEventListener("click", () => addRow(""));
  values.forEach(addRow);

  return { container, read: () => inputs.map((i) => i.value.trim()).filter(Boolean) };
}

// Références vers d'autres fiches : on montre les TITRES, pas les
// identifiants, et on signale les liens dont la fiche a disparu.
function buildRefControl(values) {
  const container = document.createElement("div");
  container.className = "chips";
  let refs = values.slice();

  const render = () => {
    container.textContent = "";
    for (const id of refs) {
      const known = cardsState.list.find((c) => c.id === id);
      const chip = document.createElement("span");
      chip.className = "chip" + (known ? "" : " chip-missing");
      chip.textContent = known ? (known.title || id) : (id + " (fiche absente)");
      const del = document.createElement("button");
      del.type = "button";
      del.textContent = "✕";
      del.addEventListener("click", () => { refs = refs.filter((x) => x !== id); render(); });
      chip.appendChild(del);
      container.appendChild(chip);
    }
    const add = document.createElement("select");
    add.className = "chip-add";
    const first = document.createElement("option");
    first.value = ""; first.textContent = "+ Lier une fiche…";
    add.appendChild(first);
    for (const c of cardsState.list) {
      if (refs.includes(c.id) || c.id === cardsState.currentId) continue;
      const opt = document.createElement("option");
      opt.value = c.id; opt.textContent = c.title || c.id;
      add.appendChild(opt);
    }
    add.addEventListener("change", () => {
      if (add.value) { refs.push(add.value); render(); }
    });
    container.appendChild(add);
  };
  render();
  return { container, read: () => refs.slice() };
}

function buildFieldControl(def, values) {
  const wrap = document.createElement("div");
  wrap.className = "form-field";
  const label = document.createElement("label");
  label.textContent = def.label || def.key;
  wrap.appendChild(label);

  let read;
  if (def.type === "longtext") {
    const area = document.createElement("textarea");
    area.value = values.join("\n");
    wrap.appendChild(area);
    read = () => (area.value.trim() ? [area.value] : []);
  } else if (def.type === "cardrefs") {
    const ctrl = buildRefControl(values);
    wrap.appendChild(ctrl.container);
    read = ctrl.read;
  } else if (def.type === "list" || def.type === "images") {
    const ctrl = buildListControl(values, def.type === "images" ? "chemin vers une image PNG" : "");
    wrap.appendChild(ctrl.container);
    read = ctrl.read;
  } else if (def.type === "chapterref") {
    // Liste déroulante des chapitres du manuscrit (H1). La valeur stockée
    // est le TITRE du chapitre. Si la valeur enregistrée ne correspond plus
    // à aucun chapitre (renommage), on la garde comme option « obsolète »
    // pour ne pas la perdre en silence.
    const select = document.createElement("select");
    const none = document.createElement("option");
    none.value = ""; none.textContent = "— vivant / non défini —";
    select.appendChild(none);
    const titles = computeChapters().map((c) => c.title);
    const current = values[0] || "";
    for (const t of titles) {
      const opt = document.createElement("option");
      opt.value = t; opt.textContent = t;
      select.appendChild(opt);
    }
    if (current && !titles.includes(current)) {
      const stale = document.createElement("option");
      stale.value = current; stale.textContent = current + " (chapitre introuvable)";
      select.appendChild(stale);
    }
    select.value = current;
    wrap.appendChild(select);
    read = () => (select.value ? [select.value] : []);
  } else { // text, number, et tout type inconnu -> saisie simple
    const input = document.createElement("input");
    input.type = (def.type === "number") ? "number" : "text";
    input.value = values[0] || "";
    wrap.appendChild(input);
    read = () => (input.value.trim() ? [input.value] : []);
  }
  return { element: wrap, read };
}

// --- Formulaire -------------------------------------------------------
function buildTagsControl(tags) {
  const container = document.getElementById("cardTags");
  let list = tags.slice();
  const render = () => {
    container.textContent = "";
    for (const tag of list) {
      const chip = document.createElement("span");
      chip.className = "chip";
      chip.textContent = tag;
      const del = document.createElement("button");
      del.type = "button";
      del.textContent = "✕";
      del.addEventListener("click", () => { list = list.filter((t) => t !== tag); render(); });
      chip.appendChild(del);
      container.appendChild(chip);
    }
    const input = document.createElement("input");
    input.className = "chip-add";
    input.placeholder = "+ tag";
    input.addEventListener("keydown", (e) => {
      if (e.key !== "Enter") return;
      e.preventDefault();
      const value = input.value.trim();
      if (value && !list.includes(value)) { list.push(value); render(); }
    });
    container.appendChild(input);
  };
  render();
  return { read: () => list.slice() };
}

function renderCardForm(card) {
  document.getElementById("cardFormEmpty").hidden = true;
  document.getElementById("cardFormBody").hidden = false;
  document.getElementById("cardFormMsg").textContent = "";
  document.getElementById("cardTitle").value = card.title || "";
  document.getElementById("cardDelete").hidden = !card.id;

  const typeSel = document.getElementById("cardType");
  typeSel.textContent = "";
  for (const schema of cardsState.schemas) {
    const opt = document.createElement("option");
    opt.value = schema.typeId;
    opt.textContent = schema.label;
    typeSel.appendChild(opt);
  }
  typeSel.value = card.type || (cardsState.schemas[0] ? cardsState.schemas[0].typeId : "");

  cardTagsReader = buildTagsControl(card.tags || []);

  const fieldsEl = document.getElementById("cardFields");
  fieldsEl.textContent = "";
  cardFieldReaders = [];
  const fields = card.fields || {};
  const schema = cardsState.schemas.find((s) => s.typeId === typeSel.value);
  const known = new Set();

  if (schema) {
    for (const def of schema.fields) {
      known.add(def.key);
      const ctrl = buildFieldControl(def, fields[def.key] || []);
      fieldsEl.appendChild(ctrl.element);
      cardFieldReaders.push({ key: def.key, read: ctrl.read });
    }
  }

  // Champs présents dans la fiche mais absents du schéma (fichier écrit
  // par une version différente) : on les AFFICHE quand même, sinon les
  // enregistrer les effacerait silencieusement.
  for (const key of Object.keys(fields)) {
    if (known.has(key) || key === "notes") continue;
    const ctrl = buildFieldControl({ key, label: key + " (hors schéma)", type: "list" }, fields[key]);
    fieldsEl.appendChild(ctrl.element);
    cardFieldReaders.push({ key, read: ctrl.read });
  }

  // Corps libre de la fiche, en Markdown.
  const notes = buildFieldControl({ key: "notes", label: "Notes libres", type: "longtext" }, fields["notes"] || []);
  fieldsEl.appendChild(notes.element);
  cardFieldReaders.push({ key: "notes", read: notes.read });

  // Rétroliens : chapitres du manuscrit qui mentionnent cette fiche
  // (lecture seule ; se met à jour à chaque affichage de la fiche).
  const backWrap = document.createElement("div");
  backWrap.className = "form-field";
  const backLabel = document.createElement("label");
  backLabel.textContent = "Mentionné dans le manuscrit";
  const backVal = document.createElement("div");
  backVal.className = "coh-sub";
  const chaps = card.title ? chaptersMentioning(card.title) : [];
  backVal.textContent = chaps.length ? chaps.join(", ") : "Nulle part pour l'instant.";
  backWrap.append(backLabel, backVal);
  fieldsEl.appendChild(backWrap);
}

// Changer de type reconstruit le formulaire en CONSERVANT ce qui a déjà
// été saisi (les champs de même clé sont repris).
document.getElementById("cardType").addEventListener("change", () => {
  const draft = collectCardForm();
  draft.type = document.getElementById("cardType").value;
  renderCardForm(draft);
});

function collectCardForm() {
  const fields = {};
  for (const { key, read } of cardFieldReaders) {
    const values = read();
    if (values.length) fields[key] = values;
  }
  return {
    id: cardsState.currentId || "",
    type: document.getElementById("cardType").value,
    title: document.getElementById("cardTitle").value.trim(),
    tags: cardTagsReader ? cardTagsReader.read() : [],
    fields,
  };
}

async function selectCard(id) {
  try {
    const r = await invoke("cards.get", { id });
    cardsState.currentId = id;
    renderCardForm(r.card);
    renderCardList();
  } catch (err) { reportError(err); }
}

function newCard() {
  cardsState.currentId = null;
  renderCardForm({ id: "", type: "", title: "", tags: [], fields: {} });
  renderCardList();
  document.getElementById("cardTitle").focus();
}

async function saveCard() {
  const draft = collectCardForm();
  if (!draft.title) {
    document.getElementById("cardFormMsg").textContent = "Donnez un titre à la fiche.";
    return;
  }
  try {
    let r = await invoke("cards.save", draft);
    if (r && r.conflict) {
      // La fiche a été modifiée en dehors de l'éditeur (autre fenêtre,
      // édition directe du fichier) depuis son chargement.
      const ok = await askConfirm(
        "Cette fiche a été modifiée en dehors de l'éditeur depuis son ouverture. " +
        "Écraser ces modifications extérieures ?", "Écraser", "Conflit de fiche");
      if (!ok) return;
      r = await invoke("cards.save", Object.assign({}, draft, { force: true }));
    }
    cardsState.currentId = r.card.id;
    await refreshCardList();
    renderCardForm(r.card);
    const msg = document.getElementById("cardFormMsg");
    if (cardsState.root) {
      msg.style.color = "var(--accent)";
      msg.textContent = "Fiche enregistrée sur disque.";
    } else {
      // Cas qui piégeait l'utilisateur : rien n'est écrit sur disque tant
      // que le manuscrit n'a pas été enregistré. On le dit clairement.
      msg.style.color = "#c0392b";
      msg.textContent = "⚠ Fiche gardée en MÉMOIRE seulement — enregistrez le manuscrit pour la conserver.";
    }
  } catch (err) { reportError(err); }
}

async function deleteCard() {
  if (!cardsState.currentId) return;
  try {
    await invoke("cards.remove", { id: cardsState.currentId });
    cardsState.currentId = null;
    document.getElementById("cardFormBody").hidden = true;
    document.getElementById("cardFormEmpty").hidden = false;
    await refreshCardList();
    setStatus("Fiche supprimée.");
  } catch (err) { reportError(err); }
}

document.getElementById("cardNew").addEventListener("click", newCard);
document.getElementById("cardSave").addEventListener("click", saveCard);
document.getElementById("cardDelete").addEventListener("click", deleteCard);
document.getElementById("cardTypeFilter").addEventListener("change", refreshCardList);
document.getElementById("cardTagFilter").addEventListener("change", refreshCardList);
document.getElementById("cardSearch").addEventListener("input", refreshCardList);

document.getElementById("cardsOpen").addEventListener("click", async () => {
  try {
    // Charger un dossier remplace la bibliothèque : même avertissement
    // qu'à l'ouverture d'un manuscrit si des fiches non sauvées existent.
    const n = await unsavedMemoryCardCount();
    if (n > 0) {
      const ok = await askConfirm(
        n + (n > 1 ? " fiches ne sont pas encore enregistrées" : " fiche n'est pas encore enregistrée") +
        " sur disque. Charger un dossier remplacera ces fiches, qui seront perdues.",
        "Charger quand même", "Fiches non enregistrées");
      if (!ok) return;
    }
    // Une bible narrative est un DOSSIER, pas un fichier : on ouvre le
    // sélecteur de dossier natif quand il est disponible.
    let path;
    if (hasNativeBridge) {
      const r = await invoke("file.chooseFolder", {});
      if (r.cancelled) return;
      path = r.path;
    } else {
      path = await askPath("open"); // repli navigateur : saisie manuelle
    }
    if (!path) return;
    const r = await invoke("cards.open", { path });
    cardsState.root = r.root || path;
    updateCardsRoot();
    cardsState.currentId = null;
    document.getElementById("cardFormBody").hidden = true;
    document.getElementById("cardFormEmpty").hidden = false;
    await refreshCardList();
    setStatus(r.count + " fiche(s) chargée(s)" + (r.skipped ? ", " + r.skipped + " ignorée(s)." : "."));
  } catch (err) { reportError(err); }
});

// =====================================================================
// 14 quater. Comparaison de deux versions
//
// Les deux textes sont posés dans UNE grille à deux colonnes plutôt que
// dans deux panneaux dont on synchroniserait le défilement. Chaque ligne
// de la grille porte les deux versions d'un même passage : elles restent
// alignées par construction, sans dérive ni saccade, et un paragraphe
// ajouté d'un côté laisse un vide en face au lieu de décaler la suite.
//
// Tout le calcul est en C++ (core/Diff) : ici on ne fait qu'afficher des
// lignes déjà appariées, avec les plages de mots à mettre en évidence.
// =====================================================================
const compareView = document.getElementById("compareView");
const compareGrid = document.getElementById("compareGrid");
// Repère de version, utile pour vérifier depuis la console que le
// navigateur sert bien le fichier courant et non une copie en cache.
window.__compareReady = true;
let compareResult = null;   // dernier résultat, pour l'interversion
let compareSwapped = false;

// Rend un côté : le bloc dans sa balise d'origine, avec les mots marqués.
function renderDiffSide(side, markClass) {
  const el = document.createElement(HEADING_TAGS[side.type] || "P");
  const cps = cpArray(side.text);
  const marks = (side.marks || []).slice().sort((a, b) => a[0] - b[0]);
  let position = 0;
  for (const [start, length] of marks) {
    if (start > position) {
      el.appendChild(document.createTextNode(cps.slice(position, start).join("")));
    }
    const mark = document.createElement("span");
    mark.className = markClass;
    mark.textContent = cps.slice(start, start + length).join("");
    el.appendChild(mark);
    position = start + length;
  }
  if (position < cps.length) {
    el.appendChild(document.createTextNode(cps.slice(position).join("")));
  }
  // Un paragraphe vide doit occuper une ligne, sinon la grille se tasse
  // et les deux colonnes se désalignent visuellement.
  if (!cps.length) el.appendChild(document.createTextNode(" "));
  return el;
}

function compareCell(side, tint, markClass) {
  const cell = document.createElement("div");
  cell.className = "cmp-cell" + (tint ? " " + tint : "");
  if (!side) {
    // Pas de vis-à-vis : on montre un vide hachuré plutôt que rien, pour
    // qu'on voie qu'il n'y a pas de texte au lieu de croire à un oubli.
    cell.classList.add("absente");
    return cell;
  }
  cell.appendChild(renderDiffSide(side, markClass));
  return cell;
}

function compareHeader(text) {
  const head = document.createElement("div");
  head.className = "cmp-entete";
  head.textContent = text;
  return head;
}

// Bandeau d'explication. Il ne s'affiche que dans le cas qui le mérite :
// une des deux versions rendue d'un seul tenant (un correcteur extérieur,
// le plus souvent). Sans lui, la comparaison par phrases et le bouton de
// redécoupage sortiraient de nulle part.
function renderCompareNotice(structure, nomAutre, parPhrases) {
  const notice = document.getElementById("compareNotice");
  const texte = document.getElementById("compareNoticeText");
  const bouton = document.getElementById("compareReflow");
  const aplati = structure.flattened;
  texte.textContent = "";
  notice.hidden = !parPhrases && aplati === "none";
  bouton.hidden = aplati !== "right";
  if (notice.hidden) return;

  const forte = document.createElement("strong");
  const suite = document.createElement("span");
  if (aplati === "right") {
    forte.textContent = "Le manuscrit ouvert a perdu ses paragraphes.";
    suite.textContent = " Tout son texte tient dans un seul bloc — c'est ce que"
      + " rendent certains correcteurs. La comparaison s'est donc faite phrase"
      + " par phrase. Le bouton ci-contre lui rend le découpage de "
      + nomAutre + ", titres compris, sans toucher au texte.";
  } else if (aplati === "left") {
    forte.textContent = "La version " + nomAutre + " a perdu ses paragraphes.";
    suite.textContent = " Son texte tient dans un seul bloc ; la comparaison"
      + " s'est donc faite phrase par phrase. Pour lui rendre sa structure,"
      + " ouvrez-la, puis comparez-la au manuscrit actuel.";
  } else {
    forte.textContent = "Comparaison phrase par phrase.";
    suite.textContent = " Les paragraphes des deux versions ne se"
      + " correspondaient plus assez pour être appariés.";
  }
  texte.append(forte, suite);
}

function renderCompare() {
  const res = compareResult;
  if (!res) return;
  compareGrid.textContent = "";

  const nomAutre = res.path.replace(/^.*[\\/]/, "");
  const gauche = compareHeader(compareSwapped ? "Version courante" : nomAutre);
  const droite = compareHeader(compareSwapped ? nomAutre : "Version courante");
  compareGrid.append(gauche, droite);

  for (const row of res.rows) {
    let tintGauche = "", tintDroite = "";
    if (row.kind === "changed") { tintGauche = "modifie"; tintDroite = "modifie"; }
    else if (row.kind === "added") { tintDroite = "ajoute"; }
    else if (row.kind === "removed") { tintGauche = "supprime"; }

    // Les marques suivent le CONTENU, pas la colonne : l'ancienne version
    // porte des suppressions même quand on l'affiche à droite.
    const cellAutre = compareCell(row.left, tintGauche, "diff-del");
    const cellCourante = compareCell(row.right, tintDroite, "diff-add");
    if (compareSwapped) compareGrid.append(cellCourante, cellAutre);
    else compareGrid.append(cellAutre, cellCourante);
  }

  const s = res.stats;
  const structure = res.structure || {};
  const parPhrases = structure.unit === "sentence";
  const unite = parPhrases ? "passage(s)" : "paragraphe(s)";
  const morceaux = [];
  if (s.changed) morceaux.push(s.changed + " " + unite + " modifié(s)");
  if (s.added) morceaux.push(s.added + " ajouté(s)");
  if (s.removed) morceaux.push(s.removed + " retiré(s)");
  if (s.wordsAdded || s.wordsRemoved) {
    morceaux.push(s.wordsAdded + " mot(s) ajouté(s), " + s.wordsRemoved + " retiré(s)");
  }
  document.getElementById("compareStats").textContent =
    res.identical ? "aucune différence" : morceaux.join(" — ");
  document.getElementById("compareTitle").textContent = "Comparaison avec " + nomAutre;
  document.getElementById("compareEmpty").hidden = !res.identical;
  compareGrid.hidden = !!res.identical;
  renderCompareNotice(structure, nomAutre, parPhrases);
  if (res.truncated) {
    setStatus("Comparaison tronquée : le manuscrit dépasse la taille affichable.");
  }
}

function showCompare(visible) {
  compareView.hidden = !visible;
  document.getElementById("workspace").hidden = visible;
  if (visible) {
    document.getElementById("cardsView").hidden = true;
    findBar.hidden = true;
  }
  document.querySelectorAll(".editor-only").forEach((el) => { el.hidden = visible; });
}

// Compare le manuscrit en mémoire au fichier donné.
async function openCompareWith(path) {
  try {
    setStatus("Comparaison en cours…");
    compareResult = await invoke("compare.withFile", { path });
    compareSwapped = false;
    renderCompare();
    showCompare(true);
    setStatus(compareResult.identical ? "Les deux versions sont identiques."
                                      : "Comparaison affichée.");
  } catch (err) { reportError(err); }
}

async function openCompareDialog() {
  try {
    const path = await askPath("open",
      { kind: "markdown", title: "Comparer avec une autre version" });
    if (!path) return;
    await openCompareWith(path);
  } catch (err) { reportError(err); }
}

document.getElementById("compareBtn").addEventListener("click", openCompareDialog);
document.getElementById("compareClose").addEventListener("click", () => {
  showCompare(false);
  compareResult = null;
  compareGrid.textContent = "";
  focusActive();
});
document.getElementById("compareSwap").addEventListener("click", () => {
  compareSwapped = !compareSwapped;
  renderCompare();
});

// Rendre au manuscrit ouvert les paragraphes de l'autre version. Le C++
// fait tout le travail (alignement des mots) ; ici on demande, on confirme,
// et on réaffiche.
document.getElementById("compareReflow").addEventListener("click", async () => {
  const res = compareResult;
  if (!res) return;
  const nomAutre = res.path.replace(/^.*[\\/]/, "");
  const ok = await askConfirm(
    "Le manuscrit ouvert va retrouver les paragraphes de « " + nomAutre + " ». "
    + "Son texte n'est pas touché : seul le découpage change, et les titres "
    + "retrouvent leur niveau. Ctrl+Z annule l'opération.",
    "Remettre en paragraphes", "Redécouper le manuscrit");
  if (!ok) return;
  try {
    setStatus("Redécoupage en cours…");
    const resultat = await invoke("compare.reparagraph", { path: res.path });
    if (!resultat.changed) {
      setStatus("Aucun découpage n'a pu être retrouvé : le manuscrit est inchangé.");
      return;
    }
    applyWholeDocument(resultat);
    // Les indices de blocs ont tous changé : le curseur repart du début.
    state.activeIndex = 0;
    state.caret = { block: 0, pos: 0 };
    // La comparaison affichée ne vaut plus rien : on la refait, et elle
    // se fera cette fois paragraphe par paragraphe.
    compareResult = await invoke("compare.withFile", { path: res.path });
    renderCompare();
    setStatus(resultat.count + " paragraphe(s) retrouvé(s).");
  } catch (err) { reportError(err); }
});

// =====================================================================
// 14 ter. Correcteur orthographique et synonymes (fin de Phase 1)
//
// Le C++ fait le travail (dictionnaire de 454 000 formes) ; l'interface
// se contente de lui demander de vérifier un bloc, de retenir les
// positions renvoyées, et de les rendre visibles.
//
// POURQUOI BLOC PAR BLOC : vérifier tout le manuscrit à chaque frappe
// serait absurde. On ne revérifie que le bloc modifié, après une courte
// pause dans la frappe. Le reste est vérifié une fois, tranquillement,
// à l'ouverture du document.
// =====================================================================
const spellState = {
  enabled: true,
  loaded: false,
  available: false,
  ranges: new Map(), // index de bloc -> [{ start, length, word }]
  timer: null,
  pending: new Set(),
};

function loadSpellPrefs() {
  try {
    const saved = localStorage.getItem("editeur.spell");
    if (saved !== null) spellState.enabled = (saved === "1");
  } catch (_) {}
}
function saveSpellPrefs() {
  try { localStorage.setItem("editeur.spell", spellState.enabled ? "1" : "0"); } catch (_) {}
}

// Table « ce caractère fait-il partie d'un mot inconnu ? » pour un bloc.
function spellMarks(index, length) {
  const marks = new Array(length).fill(false);
  if (!spellState.enabled) return marks;
  const ranges = spellState.ranges.get(index);
  if (!ranges) return marks;
  for (const range of ranges) {
    for (let k = 0; k < range.length; k++) {
      const position = range.start + k;
      if (position < length) marks[position] = true;
    }
  }
  return marks;
}

// Vérifie un bloc et redessine s'il y a du changement.
async function checkBlock(index) {
  const block = state.blocks[index];
  if (!block) { spellState.ranges.delete(index); return; }
  if (!spellState.enabled || !spellState.loaded) return;
  try {
    const r = await invoke("spell.check", { text: block.text });
    const words = r.words || [];
    const before = JSON.stringify(spellState.ranges.get(index) || []);
    if (words.length) spellState.ranges.set(index, words);
    else spellState.ranges.delete(index);
    if (JSON.stringify(words) !== before) renderBlockContent(index);
  } catch (_) { /* correcteur indisponible : on n'insiste pas */ }
}

// Frappe : on attend une pause avant de vérifier, sinon on solliciterait
// le pont à chaque lettre.
function scheduleSpellCheck(index) {
  if (!spellState.enabled || !spellState.loaded) return;
  spellState.pending.add(index);
  if (spellState.timer) clearTimeout(spellState.timer);
  spellState.timer = setTimeout(() => {
    spellState.timer = null;
    const blocks = [...spellState.pending];
    spellState.pending.clear();
    blocks.forEach((i) => checkBlock(i));
  }, 700);
}

// Passe complète, par petits paquets pour ne jamais figer l'interface.
async function checkAllBlocks() {
  if (!spellState.enabled || !spellState.loaded) return;
  for (let i = 0; i < state.blocks.length; i++) {
    await checkBlock(i);
    if (i % 20 === 19) await new Promise((r) => setTimeout(r, 0));
  }
}

function clearSpellRanges() {
  spellState.ranges.clear();
}

async function refreshSpellStatus() {
  const el = document.getElementById("spellStatus");
  try {
    const st = await invoke("spell.state", {});
    spellState.available = !!st.available;
    spellState.loaded = !!st.loaded;
    if (el) {
      if (!st.available) {
        el.textContent = "Dictionnaires absents — lancez scripts\\fetch_deps.bat.";
      } else if (st.loaded) {
        el.textContent = st.forms.toLocaleString("fr-FR") + " formes chargées" +
          (st.thesaurus ? ", " + st.thesaurusEntries.toLocaleString("fr-FR") +
                          " entrées de synonymes" : ", synonymes indisponibles") +
          (st.exclusions ? ", " + st.exclusions + " correction(s)." : ".");
      } else {
        el.textContent = st.error || "Dictionnaire non chargé.";
      }
    }
    renderPersonalList(st.personal || []);
  } catch (_) {
    if (el) el.textContent = "Correcteur indisponible.";
  }
}

// Chargement des dictionnaires : une seconde de travail, donc jamais au
// démarrage — on le déclenche une fois la fenêtre affichée.
async function loadSpellDictionaries() {
  if (!spellState.enabled) return;
  try {
    const st = await invoke("spell.load", { path: state.filePath || "" });
    spellState.available = !!st.available;
    spellState.loaded = !!st.loaded;
    await refreshSpellStatus();
    if (spellState.loaded) checkAllBlocks();
  } catch (_) { /* dictionnaires absents : le correcteur reste muet */ }
}

function renderPersonalList(words) {
  const host = document.getElementById("personalList");
  if (!host) return;
  host.textContent = "";
  if (!words.length) {
    const empty = document.createElement("div");
    empty.className = "coh-empty";
    empty.textContent = "Aucun mot ajouté pour l'instant.";
    host.appendChild(empty);
    return;
  }
  for (const word of words) {
    const row = document.createElement("div");
    row.className = "perso-row";
    const label = document.createElement("span");
    label.textContent = word;
    const remove = document.createElement("button");
    remove.textContent = "Retirer";
    remove.title = "Retirer du dictionnaire personnel";
    remove.addEventListener("click", async () => {
      try {
        await invoke("spell.removeWord", { word, path: state.filePath || "" });
        await refreshSpellStatus();
        clearSpellRanges();
        checkAllBlocks();
      } catch (err) { setSettingsMsg(err.message, true); }
    });
    row.append(label, remove);
    host.appendChild(row);
  }
}

// ---- Menu contextuel : corrections et synonymes ----------------------
const wordMenu = document.getElementById("wordMenu");

function closeWordMenu() {
  wordMenu.hidden = true;
  wordMenu.textContent = "";
}
document.addEventListener("click", (e) => {
  if (!wordMenu.hidden && !wordMenu.contains(e.target)) closeWordMenu();
});
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape" && !wordMenu.hidden) closeWordMenu();
});

function menuHead(text) {
  const head = document.createElement("div");
  head.className = "wm-head";
  head.textContent = text;
  return head;
}
function menuItem(text, action, extraClass) {
  const item = document.createElement("button");
  item.className = "wm-item" + (extraClass ? " " + extraClass : "");
  item.textContent = text;
  item.addEventListener("click", action);
  return item;
}
function menuSeparator() {
  const sep = document.createElement("div");
  sep.className = "wm-sep";
  return sep;
}

// Ligne de synonyme : cliquer le mot le substitue, cliquer la croix
// déclare qu'il n'en est pas un.
//
// POURQUOI CETTE CROIX : le thésaurus disponible comporte de vraies
// erreurs (« gâteau » y figure comme synonyme de « noir »), et aucun
// critère automatique ne permet de les repérer — la réciprocité, mesurée
// sur les 422 000 couples du fichier, écarterait surtout de bons
// synonymes. Le jugement vient donc de l'auteur, en un clic, et la
// correction est conservée dans le projet.
function synonymRow(index, wordRange, word, synonym) {
  const row = document.createElement("div");
  row.className = "wm-row";

  const label = menuItem(synonym, () => {
    if (wordRange) replaceWordAt(index, wordRange.start, wordRange.length, synonym);
  });

  const drop = document.createElement("button");
  drop.className = "wm-drop";
  drop.textContent = "✕";
  drop.title = "Ce n'est pas un synonyme de « " + word + " » — ne plus le proposer";
  drop.addEventListener("click", async (e) => {
    e.stopPropagation(); // ne pas déclencher le remplacement
    row.remove();
    try {
      await invoke("spell.excludeSynonym", { word, synonym });
      setStatus("« " + synonym + " » ne sera plus proposé pour « " + word + " ».");
    } catch (err) { reportError(err); }
  });

  row.append(label, drop);
  return row;
}

// Remplace un mot dans un bloc, en une seule étape d'annulation.
async function replaceWordAt(index, start, length, replacement) {
  closeWordMenu();
  await enqueue(async () => {
    const r = await invoke("editor.replaceRange",
                           { block: index, start, count: length, text: replacement });
    applyBlockResult(index, r);
    markDirty();
  });
  setCaret(index, start + cpLen(replacement));
  scheduleSpellCheck(index);
}

// Mot inconnu situé à cette position, s'il y en a un.
function faultAt(index, position) {
  const ranges = spellState.ranges.get(index) || [];
  for (const range of ranges) {
    if (position >= range.start && position < range.start + range.length) return range;
  }
  return null;
}

editorEl.addEventListener("contextmenu", async (e) => {
  const el = e.target.closest && e.target.closest(".block");
  if (!el) return;
  e.preventDefault();
  closeWordMenu();

  const index = Number(el.dataset.index);
  const block = state.blocks[index];
  if (!block) return;

  // Position du clic, en points de code.
  const sel = selectionInBlock(el);
  let position = sel ? sel.start : 0;
  if (document.caretRangeFromPoint) {
    const range = document.caretRangeFromPoint(e.clientX, e.clientY);
    if (range) {
      const probe = document.createRange();
      probe.selectNodeContents(el);
      probe.setEnd(range.startContainer, range.startOffset);
      position = cpLen(probe.toString());
    }
  }

  const fault = faultAt(index, position);
  const word = fault ? fault.word
                     : (() => {
                         const r = wordRangeInBlock(block, position);
                         return r ? cpArray(block.text).slice(r.start, r.end).join("") : "";
                       })();
  if (!word) return;

  wordMenu.appendChild(menuHead(word));

  if (fault) {
    try {
      const r = await invoke("spell.suggest", { word: fault.word });
      const suggestions = r.suggestions || [];
      if (suggestions.length) {
        for (const suggestion of suggestions) {
          wordMenu.appendChild(menuItem(suggestion,
            () => replaceWordAt(index, fault.start, fault.length, suggestion), "wm-suggest"));
        }
      } else {
        const none = document.createElement("div");
        none.className = "wm-empty";
        none.textContent = "Aucune correction trouvée.";
        wordMenu.appendChild(none);
      }
    } catch (_) { /* pas de suggestions : le menu reste utile */ }

    wordMenu.appendChild(menuSeparator());
    wordMenu.appendChild(menuItem("Ignorer pour cette séance", async () => {
      closeWordMenu();
      await invoke("spell.ignore", { word: fault.word });
      clearSpellRanges();
      checkAllBlocks();
    }));
    wordMenu.appendChild(menuItem("Ajouter au dictionnaire", async () => {
      closeWordMenu();
      try {
        const r = await invoke("spell.addWord",
                               { word: fault.word, path: state.filePath || "" });
        clearSpellRanges();
        await checkAllBlocks();
        await refreshSpellStatus();
        setStatus(r.saved ? "« " + fault.word + " » ajouté au dictionnaire du projet."
                          : "« " + fault.word + " » ajouté — enregistrez le manuscrit " +
                            "pour conserver ce mot.");
      } catch (err) { reportError(err); }
    }));
  }

  // Synonymes : proposés pour tout mot, fautif ou non.
  //
  // Huit au maximum, et non douze : le thésaurus mêle de vrais synonymes
  // à des mots simplement associés, et les scories arrivent en fin de
  // liste. Sur « fenêtre », les huit premiers sont tous justes.
  try {
    const r = await invoke("spell.synonyms", { word });
    const meanings = r.meanings || [];
    if (meanings.length) {
      wordMenu.appendChild(menuSeparator());
      wordMenu.appendChild(menuHead("Synonymes"));
      const wordRange = fault ? { start: fault.start, length: fault.length }
                              : (() => {
                                  const wr = wordRangeInBlock(block, position);
                                  return wr ? { start: wr.start, length: wr.end - wr.start }
                                            : null;
                                })();
      let shown = 0;
      for (const meaning of meanings) {
        if (shown >= 8) break;
        wordMenu.appendChild(menuHead(meaning.label));
        for (const synonym of meaning.synonyms) {
          if (shown >= 8) break;
          shown++;
          wordMenu.appendChild(synonymRow(index, wordRange, word, synonym));
        }
      }
    }
  } catch (_) { /* thésaurus absent : on n'affiche rien de plus */ }

  if (!wordMenu.childNodes.length) return;
  // Placement : sous le curseur, en restant dans la fenêtre.
  wordMenu.hidden = false;
  const rect = wordMenu.getBoundingClientRect();
  const x = Math.min(e.clientX, window.innerWidth - rect.width - 8);
  const y = Math.min(e.clientY, window.innerHeight - rect.height - 8);
  wordMenu.style.left = Math.max(4, x) + "px";
  wordMenu.style.top = Math.max(4, y) + "px";
});

// ---- Réglages --------------------------------------------------------
document.getElementById("spellEnabled").addEventListener("change", async (e) => {
  spellState.enabled = e.target.checked;
  saveSpellPrefs();
  if (spellState.enabled) {
    if (!spellState.loaded) await loadSpellDictionaries();
    else checkAllBlocks();
  } else {
    clearSpellRanges();
    renderAll();
    setCaret(state.caret.block, state.caret.pos);
  }
});

// =====================================================================
// 14 bis. Export du livre (Phase 4)
//
// Deux sorties, un seul contenu :
//   - PDF  : c'est le MOTEUR DE RENDU du webview qui pagine (cf. cadrage).
//            On lui donne à imprimer cette page, préalablement basculée
//            en « vue d'impression » (#printRoot seul visible, règles
//            @media print). Aucun convertisseur maison, donc rien qui
//            puisse rendre autrement que ce que l'auteur voit.
//   - EPUB : construit en C++ (io/Epub), qui repart du MODÈLE et non du
//            DOM — un EPUB est un ensemble de fichiers XHTML, pas une
//            page imprimée.
//
// La vue d'impression est construite en createElement/textContent, comme
// tout le reste de l'interface : aucun innerHTML, donc aucun texte de
// l'auteur ne peut être interprété comme du balisage.
// =====================================================================
const exportDialog = document.getElementById("exportDialog");
const printRoot = document.getElementById("printRoot");

function exportKind() {
  const chosen = document.querySelector('input[name="exportKind"]:checked');
  return chosen ? chosen.value : "manuscript";
}
function exportMsg(text, isError) {
  const el = document.getElementById("exportMsg");
  el.textContent = text || "";
  el.classList.toggle("error", !!isError);
}

// Dossier de référence pour les chemins RELATIFS (couverture,
// illustrations) : celui du manuscrit, ou celui des fiches pour la bible.
function exportBaseDir(kind) {
  if (kind === "bible") {
    return cardsState.root || cardsFolderFor(state.filePath) || "";
  }
  const p = state.filePath || "";
  const cut = Math.max(p.lastIndexOf("\\"), p.lastIndexOf("/"));
  return cut > 0 ? p.slice(0, cut) : "";
}

// Nom de fichier proposé, épuré des caractères interdits par le système.
function exportFileName(kind, extension) {
  const raw = (document.getElementById("exportTitle").value || "mon-roman").trim();
  const safe = raw.replace(/[\\/:*?"<>|]/g, "-").slice(0, 80) || "mon-roman";
  return safe + (kind === "bible" ? " - bible narrative" : "") + extension;
}

async function openExportDialog() {
  try {
    const m = await invoke("file.getMetadata", {});
    document.getElementById("exportTitle").value = m.title || "";
    document.getElementById("exportAuthor").value = m.author || "";
    document.getElementById("exportCover").value = m.cover || "";
    document.getElementById("exportSummary").value = m.summary || "";
    exportMsg("");
    await refreshCoverPreview();
    exportDialog.showModal();
  } catch (err) { reportError(err); }
}

// Les métadonnées saisies ici sont celles du manuscrit : on les renvoie
// au C++ avant d'exporter, pour que le livre et le fichier .md portent
// exactement les mêmes.
async function saveExportMetadata() {
  await invoke("file.setMetadata", {
    title: document.getElementById("exportTitle").value,
    author: document.getElementById("exportAuthor").value,
    cover: document.getElementById("exportCover").value,
    summary: document.getElementById("exportSummary").value,
  });
  markDirty();
}

// Aperçu de la couverture. Le webview ne peut pas charger une image par
// un chemin file:// (la page est fournie par set_html, sans origine) :
// le C++ nous la renvoie donc encodée en data URI.
async function coverDataUri(kind) {
  const path = document.getElementById("exportCover").value.trim();
  if (!path) return "";
  try {
    const r = await invoke("export.image", { path, base: exportBaseDir(kind) });
    return r.dataUri || "";
  } catch (_) {
    return ""; // couverture illisible : le livre sort sans, sans bloquer
  }
}

async function refreshCoverPreview() {
  const img = document.getElementById("exportCoverPreview");
  const uri = await coverDataUri(exportKind());
  if (uri) { img.src = uri; img.hidden = false; }
  else { img.removeAttribute("src"); img.hidden = true; }
}

document.getElementById("exportBtn").addEventListener("click", openExportDialog);
document.getElementById("exportClose").addEventListener("click", () => exportDialog.close());
document.getElementById("exportCover").addEventListener("change", refreshCoverPreview);
document.getElementById("exportCoverPick").addEventListener("click", async () => {
  try {
    const path = await askPath("open", { kind: "image", title: "Choisir une couverture" });
    if (!path) return;
    document.getElementById("exportCover").value = path;
    await refreshCoverPreview();
  } catch (err) { exportMsg(err.message, true); }
});

// ---- Construction de la vue d'impression ----------------------------

// Rendu d'un bloc pour le livre : mêmes styles qu'à l'écran, mais sans
// les mentions de fiches (aide à l'écriture, pas contenu du livre).
function buildPrintBlockChildren(block) {
  const { cps, masks, sizes } = styleTable(block);
  const frag = document.createDocumentFragment();
  let i = 0;
  while (i < cps.length) {
    let j = i + 1;
    while (j < cps.length && sameSet(masks[i], masks[j]) && sizes[i] === sizes[j]) j++;
    frag.appendChild(makeSegment(cps.slice(i, j).join(""), masks[i], sizes[i]));
    i = j;
  }
  return frag;
}

function printElement(tag, className, text) {
  const el = document.createElement(tag);
  if (className) el.className = className;
  if (text !== undefined) el.textContent = text;
  return el;
}

async function buildTitlePage(kind) {
  const section = printElement("section", "pv-title-page");
  const uri = await coverDataUri(kind);
  if (uri) {
    const img = document.createElement("img");
    img.className = "pv-cover";
    img.alt = "Couverture";
    img.src = uri;
    section.appendChild(img);
    // On attend le décodage : sinon l'impression peut partir avant que
    // l'image ne soit prête, et la couverture manquerait dans le PDF.
    try { await img.decode(); } catch (_) {}
  }
  const title = document.getElementById("exportTitle").value.trim() || "Sans titre";
  section.appendChild(printElement("h1", "pv-book-title",
    kind === "bible" ? "Bible narrative — " + title : title));
  const author = document.getElementById("exportAuthor").value.trim();
  if (author) section.appendChild(printElement("p", "pv-author", author));
  const summary = document.getElementById("exportSummary").value.trim();
  if (summary) section.appendChild(printElement("p", "pv-summary", summary));
  return section;
}

function buildToc(entries) {
  const section = printElement("section", "pv-toc");
  section.appendChild(printElement("h1", null, "Table des matières"));
  const list = document.createElement("ol");
  for (const entry of entries) {
    const li = printElement("li", entry.level >= 3 ? "lvl3" : (entry.level === 2 ? "lvl2" : ""),
                            entry.title);
    list.appendChild(li);
  }
  section.appendChild(list);
  return section;
}

function buildManuscriptPrint(container) {
  let first = true;
  for (const block of state.blocks) {
    if (block.type === "p" && !block.text.trim()) continue; // paragraphe vide
    const el = document.createElement(HEADING_TAGS[block.type] || "P");
    el.appendChild(buildPrintBlockChildren(block));
    // Le tout premier titre ne doit pas provoquer une page blanche.
    if (block.type === "h1" && first) el.classList.add("pv-first");
    if (block.type === "h1") first = false;
    container.appendChild(el);
  }
}

// Un champ de fiche dans la bible imprimée. Les images sont demandées au
// C++ en data URI (même raison que la couverture).
async function appendCardField(container, label, type, values, base) {
  if (!values || !values.length) return;
  if (type === "images") {
    for (const path of values) {
      let uri = "";
      try {
        const r = await invoke("export.image", { path, base });
        uri = r.dataUri || "";
      } catch (_) { uri = ""; }
      if (uri) {
        const figure = document.createElement("figure");
        const img = document.createElement("img");
        img.src = uri;
        img.alt = label;
        figure.appendChild(img);
        container.appendChild(figure);
        try { await img.decode(); } catch (_) {}
      } else {
        // Illustration introuvable : on garde la trace du fichier attendu.
        container.appendChild(fieldParagraph(label, path));
      }
    }
    return;
  }
  if (type === "list") {
    const head = printElement("p", "pv-field");
    head.appendChild(printElement("span", "pv-field-label", label));
    container.appendChild(head);
    const list = printElement("ul", "pv-list");
    for (const v of values) list.appendChild(printElement("li", null, v));
    container.appendChild(list);
    return;
  }
  if (type === "longtext") {
    const head = printElement("p", "pv-field");
    head.appendChild(printElement("span", "pv-field-label", label));
    container.appendChild(head);
    for (const v of values) {
      for (const line of String(v).split("\n")) {
        if (line.trim()) container.appendChild(printElement("p", null, line));
      }
    }
    return;
  }
  container.appendChild(fieldParagraph(label, values.join(", ")));
}

function fieldParagraph(label, value) {
  const p = printElement("p", "pv-field");
  p.appendChild(printElement("span", "pv-field-label", label + " :"));
  p.appendChild(document.createTextNode(" " + value));
  return p;
}

// Bible narrative : un chapitre par type de fiche, une section par fiche.
// Les libellés viennent des SCHÉMAS (cards.schemas) : ajouter un type de
// fiche l'ajoute automatiquement à l'export, sans toucher à ce code.
async function buildBiblePrint(container) {
  await initCardsView(); // s'assure que les schémas sont chargés
  const base = exportBaseDir("bible");
  const chapters = []; // pour la table des matières
  let first = true;

  for (const schema of cardsState.schemas) {
    const listed = await invoke("cards.list", { type: schema.typeId });
    const cards = listed.cards || [];
    if (!cards.length) continue; // pas de chapitre vide

    container.appendChild(printElement("h1", first ? "pv-first" : null, schema.label));
    chapters.push({ level: 1, title: schema.label });
    first = false;

    for (const summary of cards) {
      const got = await invoke("cards.get", { id: summary.id });
      const card = got.card || {};
      container.appendChild(printElement("h2", null, card.title || card.id));
      chapters.push({ level: 2, title: card.title || card.id });
      if (card.tags && card.tags.length) {
        container.appendChild(fieldParagraph("Tags", card.tags.join(", ")));
      }
      const fields = card.fields || {};
      const known = new Set();
      for (const def of schema.fields) {
        known.add(def.key);
        await appendCardField(container, def.label, def.type, fields[def.key], base);
      }
      // Champs hors schéma (fichier d'une autre version) : jamais perdus.
      for (const key of Object.keys(fields)) {
        if (known.has(key) || key === "notes") continue;
        await appendCardField(container, key, "text", fields[key], base);
      }
      await appendCardField(container, "Notes", "longtext", fields["notes"], base);
    }
  }
  return chapters;
}

// Le contenu est construit AVANT d'être inséré : la table des matières a
// besoin de connaître les chapitres, et elle se place avant eux.
async function buildPrintView(kind) {
  printRoot.textContent = "";
  const titlePage = document.getElementById("exportTitlePage").checked
                      ? await buildTitlePage(kind) : null;

  const content = document.createDocumentFragment();
  let entries;
  if (kind === "bible") {
    entries = await buildBiblePrint(content);
    if (!entries.length) throw new Error("Aucune fiche à exporter.");
    entries = entries.filter((e) => e.level === 1); // types de fiches seulement
  } else {
    buildManuscriptPrint(content);
    if (!content.childNodes.length) {
      throw new Error("Le manuscrit est vide : rien à exporter.");
    }
    entries = computeOutline().filter((e) => !e.preamble);
  }

  if (titlePage) printRoot.appendChild(titlePage);
  if (document.getElementById("exportToc").checked && entries.length) {
    printRoot.appendChild(buildToc(entries));
  }
  printRoot.appendChild(content);
}

function clearPrintView() {
  document.body.classList.remove("printing");
  printRoot.hidden = true;
  printRoot.textContent = "";
}

// ---- Export PDF ------------------------------------------------------
// PrintToPdf (côté C++) est asynchrone : la commande rend la main tout de
// suite, et la fin arrive par cet appel. Sans cela, attendre la fin dans
// le gestionnaire bloquerait la boucle de messages du webview — et donc
// l'impression elle-même.
let pdfDoneResolve = null;
window.__exportPdfDone = function (result) {
  if (pdfDoneResolve) {
    const resolve = pdfDoneResolve;
    pdfDoneResolve = null;
    resolve(result || { ok: false, error: "Réponse d'impression vide." });
  }
};

function waitForPdf(timeoutMs) {
  return new Promise((resolve) => {
    pdfDoneResolve = resolve;
    setTimeout(() => {
      if (pdfDoneResolve === resolve) {
        pdfDoneResolve = null;
        resolve({ ok: false, error: "L'impression n'a pas répondu à temps." });
      }
    }, timeoutMs);
  });
}

// Marges saisies en centimètres (unité familière), transmises en pouces
// (unité de l'API d'impression).
function marginInches() {
  const cm = Number(document.getElementById("exportMargin").value);
  if (!isFinite(cm) || cm < 0) return 0.79; // 2 cm par défaut
  return Math.min(cm, 7) / 2.54;
}

async function doExportPdf() {
  const kind = exportKind();
  try {
    await saveExportMetadata();
  } catch (err) { exportMsg(err.message, true); return; }

  const path = await askPath("save",
    { kind: "pdf", name: exportFileName(kind, ".pdf"), title: "Exporter en PDF" });
  if (!path) return;

  exportDialog.close();
  setStatus("Préparation de l'export PDF…");
  try {
    await buildPrintView(kind);
  } catch (err) { clearPrintView(); reportError(err); return; }

  document.body.classList.add("printing");
  printRoot.hidden = false;
  // Laisse le moteur calculer la mise en page avant d'imprimer.
  await new Promise((r) => setTimeout(r, 120));

  try {
    if (!hasNativeBridge) {
      // Hors application : la boîte d'impression du navigateur, où
      // « Enregistrer au format PDF » fait le même travail.
      window.print();
      setStatus("Aperçu : impression déléguée au navigateur.");
      return;
    }
    await invoke("export.pdf", {
      path,
      pageSize: document.getElementById("exportPageSize").value,
      margin: marginInches(),
    });
    setStatus("Impression du PDF en cours…");
    const done = await waitForPdf(180000);
    if (!done.ok) throw new Error(done.error || "Échec de l'export PDF.");
    setStatus("PDF exporté : " + done.path);
  } catch (err) {
    // Repli : la boîte d'impression du système (runtime WebView2 trop
    // ancien, ou impression directe indisponible). L'auteur peut y
    // choisir « Enregistrer au format PDF ».
    setStatus("Impression directe indisponible (" + err.message +
              ") — passage par la boîte d'impression du système.");
    try { window.print(); } catch (_) {}
  } finally {
    clearPrintView();
  }
}

// ---- Impression sur une vraie imprimante -----------------------------
// Même vue que l'export PDF (mêmes options, même mise en page) : seule
// la destination change. On passe par window.print(), qui ouvre la boîte
// d'impression du moteur — c'est là que l'auteur choisit son imprimante,
// son papier et ses marges.
//
// Subtilité : selon les versions du moteur, window.print() rend la main
// TOUT DE SUITE ou seulement à la fermeture de la boîte. On ne remet donc
// pas l'éditeur en place juste après l'appel (on risquerait de vider la
// page pendant qu'elle s'imprime) : on attend le signal « impression
// terminée », le retour du focus sur la fenêtre, ou, en dernier recours,
// un délai de sécurité.
async function doPrintBook() {
  const kind = exportKind();
  try {
    await saveExportMetadata();
  } catch (err) { exportMsg(err.message, true); return; }

  exportDialog.close();
  setStatus("Préparation de l'impression…");
  try {
    await buildPrintView(kind);
  } catch (err) { clearPrintView(); reportError(err); return; }

  document.body.classList.add("printing");
  printRoot.hidden = false;
  await new Promise((r) => setTimeout(r, 120)); // laisse la mise en page se poser

  let restored = false;
  const restore = () => {
    if (restored) return;
    restored = true;
    window.removeEventListener("afterprint", restore);
    window.removeEventListener("focus", restore);
    clearPrintView();
    setStatus("Impression terminée.");
  };
  window.addEventListener("afterprint", restore);
  window.addEventListener("focus", restore);
  // Filet de sécurité : si aucun de ces signaux n'arrive (moteur qui
  // n'émet pas afterprint et fenêtre qui garde le focus), l'éditeur
  // revient quand même au bout d'une minute.
  setTimeout(restore, 60000);

  try {
    window.print();
  } catch (err) {
    restore();
    reportError(err);
  }
}
document.getElementById("exportPrintBtn").addEventListener("click", doPrintBook);

// ---- Export EPUB -----------------------------------------------------
async function doExportEpub() {
  const kind = exportKind();
  try {
    await saveExportMetadata();
    const path = await askPath("save",
      { kind: "epub", name: exportFileName(kind, ".epub"), title: "Exporter en EPUB" });
    if (!path) return;
    exportMsg("Export en cours…");
    const r = await invoke("export.epub", {
      path,
      kind,
      base: exportBaseDir(kind),
      titlePage: document.getElementById("exportTitlePage").checked,
      toc: document.getElementById("exportToc").checked,
    });
    const ko = Math.max(1, Math.round((r.bytes || 0) / 1024));
    exportMsg("");
    exportDialog.close();
    setStatus("EPUB exporté : " + r.path + " — " + r.chapters +
              " chapitre(s), " + ko + " Ko" +
              (r.images ? ", " + r.images + " image(s)" : "") + ".");
  } catch (err) {
    exportMsg(err.message, true);
  }
}

document.getElementById("exportPdfBtn").addEventListener("click", doExportPdf);
document.getElementById("exportEpubBtn").addEventListener("click", doExportEpub);

// =====================================================================
// 15. Messages d'état / erreurs
// =====================================================================
function setStatus(msg) {
  const el = document.getElementById("status-msg");
  el.textContent = msg;
  el.classList.remove("error");
}
function reportError(err) {
  const el = document.getElementById("status-msg");
  el.textContent = "Erreur : " + (err && err.message ? err.message : String(err));
  el.classList.add("error");
  // Le miroir a pu diverger du C++ : on resynchronise par sécurité.
  invoke("editor.getDocument", {}).then((doc) => {
    state.blocks = doc.blocks; renderAll();
  }).catch(() => {});
}

// =====================================================================
// 16. Démarrage
// =====================================================================
async function init() {
  try { setZoom(Number(localStorage.getItem("editeur.zoom")) || 100); } catch (_) { setZoom(100); }
  try { setTheme(localStorage.getItem("editeur.theme") || "clair"); } catch (_) { setTheme("clair"); }
  try { chapterPanel.hidden = localStorage.getItem("editeur.chapters") !== "1"; } catch (_) {}

  const doc = await invoke("editor.getDocument", {});
  state.blocks = (doc.blocks && doc.blocks.length) ? doc.blocks : [{ type: "p", text: "", runs: [] }];
  renderAll();
  setCaret(0, 0);
  markClean(); // document neuf : rien à enregistrer
  restartSession();
  updateSessionIndicator();
  checkReminders(); // échéances déjà atteintes au démarrage
  await refreshStatus();
  setStatus(hasNativeBridge ? "Prêt." : "Aperçu (moteur simulé — hors application).");

  // Correcteur : les dictionnaires demandent environ une seconde de
  // travail. On les charge APRÈS l'affichage, pour que l'éditeur soit
  // utilisable immédiatement — écrire ne dépend pas du correcteur.
  loadSpellPrefs();
  const spellBox = document.getElementById("spellEnabled");
  if (spellBox) spellBox.checked = spellState.enabled;
  setTimeout(() => { loadSpellDictionaries(); }, 300);
}

init();

})();
