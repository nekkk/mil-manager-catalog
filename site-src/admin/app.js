(function () {
  const STORAGE_KEY = "mil-catalog-admin-config";
  const API_ROOT = "https://api.github.com";
  const METADATA_PATH = "catalog-source/catalog-metadata.json";
  const ENTRIES_PATH = "catalog-source/entries";
  const GENERATED_INDEX_PATH = "dist/index.json";

  const state = {
    config: {
      owner: "",
      repo: "",
      branch: "main",
      token: "",
    },
    metadata: {
      catalogName: "MIL Traducoes",
      channel: "stable",
      schemaVersion: "1.0",
      catalogRevision: "",
      defaults: {
        author: "M.I.L.",
        detailsUrl: "https://miltraducoes.com/",
        language: "pt-BR",
      },
    },
    entries: [],
    fileShas: new Map(),
    originalPaths: new Map(),
    derivedEntries: new Map(),
    deletedPaths: new Set(),
    selectedId: null,
    isLoaded: false,
  };

  const els = {
    owner: document.getElementById("owner"),
    repo: document.getElementById("repo"),
    branch: document.getElementById("branch"),
    token: document.getElementById("token"),
    loadRepo: document.getElementById("loadRepo"),
    clearSession: document.getElementById("clearSession"),
    catalogName: document.getElementById("catalogName"),
    channel: document.getElementById("channel"),
    schemaVersion: document.getElementById("schemaVersion"),
    catalogRevision: document.getElementById("catalogRevision"),
    defaultAuthor: document.getElementById("defaultAuthor"),
    defaultLanguage: document.getElementById("defaultLanguage"),
    defaultDetailsUrl: document.getElementById("defaultDetailsUrl"),
    status: document.getElementById("status"),
    search: document.getElementById("search"),
    entryList: document.getElementById("entryList"),
    newEntry: document.getElementById("newEntry"),
    duplicateEntry: document.getElementById("duplicateEntry"),
    saveEntry: document.getElementById("saveEntry"),
    deleteEntry: document.getElementById("deleteEntry"),
    publishAll: document.getElementById("publishAll"),
    entryId: document.getElementById("entryId"),
    section: document.getElementById("section"),
    titleId: document.getElementById("titleId"),
    language: document.getElementById("language"),
    name: document.getElementById("name"),
    introPtBr: document.getElementById("introPtBr"),
    introEnUs: document.getElementById("introEnUs"),
    summaryPtBr: document.getElementById("summaryPtBr"),
    summaryEnUs: document.getElementById("summaryEnUs"),
    author: document.getElementById("author"),
    packageVersion: document.getElementById("packageVersion"),
    contentRevision: document.getElementById("contentRevision"),
    downloadUrl: document.getElementById("downloadUrl"),
    detailsUrl: document.getElementById("detailsUrl"),
    coverUrl: document.getElementById("coverUrl"),
    thumbnailUrl: document.getElementById("thumbnailUrl"),
    tags: document.getElementById("tags"),
    minGameVersion: document.getElementById("minGameVersion"),
    maxGameVersion: document.getElementById("maxGameVersion"),
    exactGameVersions: document.getElementById("exactGameVersions"),
    featured: document.getElementById("featured"),
    entryMeta: document.getElementById("entryMeta"),
  };

  function setStatus(message, tone = "") {
    els.status.textContent = message || "";
    els.status.className = `status${tone ? ` ${tone}` : ""}`;
  }

  function loadConfigFromStorage() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) {
        return;
      }
      const parsed = JSON.parse(raw);
      state.config.owner = parsed.owner || "";
      state.config.repo = parsed.repo || "";
      state.config.branch = parsed.branch || "main";
      state.config.token = parsed.token || "";
    } catch (error) {
      console.warn(error);
    }
  }

  function saveConfigToStorage() {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(state.config));
  }

  function syncConfigFromInputs() {
    state.config.owner = els.owner.value.trim();
    state.config.repo = els.repo.value.trim();
    state.config.branch = els.branch.value.trim() || "main";
    state.config.token = els.token.value.trim();
  }

  function syncInputsFromConfig() {
    els.owner.value = state.config.owner;
    els.repo.value = state.config.repo;
    els.branch.value = state.config.branch;
    els.token.value = state.config.token;
  }

  function repoApi(path) {
    const owner = encodeURIComponent(state.config.owner);
    const repo = encodeURIComponent(state.config.repo);
    const branch = encodeURIComponent(state.config.branch);
    return `${API_ROOT}/repos/${owner}/${repo}/contents/${path}?ref=${branch}`;
  }

  async function githubFetch(url, options = {}) {
    const headers = new Headers(options.headers || {});
    headers.set("Accept", "application/vnd.github+json");
    headers.set("Authorization", `Bearer ${state.config.token}`);
    headers.set("X-GitHub-Api-Version", "2022-11-28");

    const response = await fetch(url, { ...options, headers });
    if (!response.ok) {
      const text = await response.text();
      throw new Error(`GitHub API ${response.status}: ${text || response.statusText}`);
    }
    return response;
  }

  function decodeContent(base64Value) {
    const normalized = (base64Value || "").replace(/\n/g, "");
    const binary = atob(normalized);
    const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  }

  function encodeContent(text) {
    const bytes = new TextEncoder().encode(text);
    let binary = "";
    bytes.forEach((byte) => {
      binary += String.fromCharCode(byte);
    });
    return btoa(binary);
  }

  async function fetchJsonFile(path) {
    const response = await githubFetch(repoApi(path));
    const payload = await response.json();
    const content = JSON.parse(decodeContent(payload.content));
    state.fileShas.set(path, payload.sha);
    return { content, sha: payload.sha, path };
  }

  async function fetchEntries() {
    const response = await githubFetch(repoApi(ENTRIES_PATH));
    const payload = await response.json();
    const files = payload.filter((item) => item.type === "file" && item.name.endsWith(".json"));
    const result = [];

    for (const file of files) {
      const fetched = await fetchJsonFile(file.path);
      result.push({ content: fetched.content, path: fetched.path, sha: fetched.sha });
    }

    result.sort((a, b) => (a.content.name || "").localeCompare(b.content.name || "", "pt-BR"));
    return result;
  }

  async function fetchGeneratedIndex() {
    try {
      const fetched = await fetchJsonFile(GENERATED_INDEX_PATH);
      const entries = Array.isArray(fetched.content?.entries) ? fetched.content.entries : [];
      return entries;
    } catch (error) {
      console.warn("Nao foi possivel carregar o indice gerado:", error);
      return [];
    }
  }

  function slugify(value) {
    return value
      .normalize("NFD")
      .replace(/[\u0300-\u036f]/g, "")
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-+|-+$/g, "")
      .replace(/-{2,}/g, "-");
  }

  function splitList(value) {
    return value
      .split(",")
      .map((item) => item.trim())
      .filter(Boolean);
  }

  function buildEntryFromForm() {
    const compatibility = {};
    const minVersion = els.minGameVersion.value.trim();
    const maxVersion = els.maxGameVersion.value.trim();
    const exactVersions = splitList(els.exactGameVersions.value);

    if (minVersion) {
      compatibility.minGameVersion = minVersion;
    }
    if (maxVersion) {
      compatibility.maxGameVersion = maxVersion;
    }
    if (exactVersions.length) {
      compatibility.exactGameVersions = exactVersions;
    }

    const entry = {
      id: els.entryId.value.trim(),
      section: els.section.value.trim(),
      titleId: els.titleId.value.trim().toUpperCase(),
      name: els.name.value.trim(),
      introPtBr: els.introPtBr.value.trim(),
      introEnUs: els.introEnUs.value.trim(),
      summary: els.summaryPtBr.value.trim(),
      summaryPtBr: els.summaryPtBr.value.trim(),
      summaryEnUs: els.summaryEnUs.value.trim(),
      author: els.author.value.trim(),
      packageVersion: els.packageVersion.value.trim(),
      contentRevision: els.contentRevision.value.trim(),
      language: els.language.value.trim(),
      downloadUrl: els.downloadUrl.value.trim(),
      detailsUrl: els.detailsUrl.value.trim(),
      coverUrl: els.coverUrl.value.trim(),
      thumbnailUrl: els.thumbnailUrl.value.trim(),
      tags: splitList(els.tags.value),
      compatibility,
      featured: !!els.featured.checked,
    };

    Object.keys(entry).forEach((key) => {
      if (entry[key] === "" || (Array.isArray(entry[key]) && entry[key].length === 0)) {
        delete entry[key];
      }
    });
    if (Object.keys(compatibility).length === 0) {
      delete entry.compatibility;
    }
    return entry;
  }

  function findSelectedEntry() {
    return state.entries.find((entry) => entry.id === state.selectedId) || null;
  }

  function entryPath(entry) {
    return `${ENTRIES_PATH}/${entry.id}.json`;
  }

  function renderEntryMeta(entry, originalPath) {
    els.entryMeta.innerHTML = "";
    const pills = [
      `arquivo: ${originalPath || entryPath(entry)}`,
      `titleId: ${entry.titleId || "sem titleId"}`,
      entry.featured ? "destaque" : "normal",
    ];
    for (const label of pills) {
      const span = document.createElement("span");
      span.className = "pill";
      span.textContent = label;
      els.entryMeta.appendChild(span);
    }
  }

  function fillEntryForm(entry) {
    if (!entry) {
      [
        "entryId",
        "titleId",
        "language",
        "name",
        "introPtBr",
        "introEnUs",
        "summaryPtBr",
        "summaryEnUs",
        "author",
        "packageVersion",
        "contentRevision",
        "downloadUrl",
        "detailsUrl",
        "coverUrl",
        "thumbnailUrl",
        "tags",
        "minGameVersion",
        "maxGameVersion",
        "exactGameVersions",
      ].forEach((key) => {
        els[key].value = "";
      });
      els.section.value = "translations";
      els.featured.checked = false;
      els.entryMeta.innerHTML = "";
      return;
    }

    const derived = state.derivedEntries.get(entry.id) || {};

    els.entryId.value = entry.id || "";
    els.section.value = entry.section || "translations";
    els.titleId.value = entry.titleId || "";
    els.language.value = entry.language || "";
    els.name.value = entry.name || "";
    els.introPtBr.value = entry.introPtBr || entry.intro || derived.introPtBr || derived.intro || "";
    els.introEnUs.value = entry.introEnUs || derived.introEnUs || "";
    els.summaryPtBr.value = entry.summaryPtBr || entry.summary || derived.summaryPtBr || derived.summary || "";
    els.summaryEnUs.value = entry.summaryEnUs || derived.summaryEnUs || "";
    els.author.value = entry.author || "";
    els.packageVersion.value = entry.packageVersion || "";
    els.contentRevision.value = entry.contentRevision || "";
    els.downloadUrl.value = entry.downloadUrl || "";
    els.detailsUrl.value = entry.detailsUrl || "";
    els.coverUrl.value = entry.coverUrl || derived.coverUrl || "";
    els.thumbnailUrl.value = entry.thumbnailUrl || derived.thumbnailUrl || derived.iconUrl || "";
    els.tags.value = (entry.tags || []).join(", ");
    els.minGameVersion.value = entry.compatibility?.minGameVersion || "";
    els.maxGameVersion.value = entry.compatibility?.maxGameVersion || "";
    els.exactGameVersions.value = (entry.compatibility?.exactGameVersions || []).join(", ");
    els.featured.checked = !!entry.featured;
    renderEntryMeta(entry, state.originalPaths.get(entry.id));
  }

  function renderEntryList() {
    const term = els.search.value.trim().toLowerCase();
    els.entryList.innerHTML = "";

    const filtered = state.entries.filter((entry) => {
      if (!term) {
        return true;
      }
      return [entry.id, entry.name, entry.titleId]
        .filter(Boolean)
        .some((value) => value.toLowerCase().includes(term));
    });

    for (const entry of filtered) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = `entry-item${entry.id === state.selectedId ? " active" : ""}`;
      button.innerHTML = `
        <div class="title">${escapeHtml(entry.name || entry.id || "(sem nome)")}</div>
        <div class="meta">${escapeHtml(entry.section || "")} • ${escapeHtml(entry.titleId || "")}</div>
      `;
      button.addEventListener("click", () => {
        saveCurrentEntry(false);
        state.selectedId = entry.id;
        fillEntryForm(entry);
        renderEntryList();
      });
      els.entryList.appendChild(button);
    }
  }

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/\"/g, "&quot;");
  }

  function syncMetadataFromInputs() {
    state.metadata.catalogName = els.catalogName.value.trim();
    state.metadata.channel = els.channel.value.trim();
    state.metadata.schemaVersion = els.schemaVersion.value.trim();
    state.metadata.catalogRevision = els.catalogRevision.value.trim();
    state.metadata.defaults = {
      author: els.defaultAuthor.value.trim(),
      language: els.defaultLanguage.value.trim(),
      detailsUrl: els.defaultDetailsUrl.value.trim(),
    };
  }

  function syncInputsFromMetadata() {
    els.catalogName.value = state.metadata.catalogName || "";
    els.channel.value = state.metadata.channel || "";
    els.schemaVersion.value = state.metadata.schemaVersion || "";
    els.catalogRevision.value = state.metadata.catalogRevision || "";
    els.defaultAuthor.value = state.metadata.defaults?.author || "";
    els.defaultLanguage.value = state.metadata.defaults?.language || "";
    els.defaultDetailsUrl.value = state.metadata.defaults?.detailsUrl || "";
  }

  function validateEntry(entry) {
    const missing = [];
    ["id", "section", "titleId", "name", "downloadUrl"].forEach((field) => {
      if (!entry[field]) {
        missing.push(field);
      }
    });
    if (missing.length) {
      throw new Error(`Entrada incompleta: faltam ${missing.join(", ")}`);
    }
  }

  function saveCurrentEntry(showStatus = true) {
    const current = findSelectedEntry();
    if (!current) {
      return;
    }

    const updated = buildEntryFromForm();
    validateEntry(updated);

    const newId = updated.id;
    const existingOther = state.entries.find((entry) => entry.id === newId && entry.id !== state.selectedId);
    if (existingOther) {
      throw new Error(`Ja existe uma entrada com id '${newId}'.`);
    }

    const oldPath = state.originalPaths.get(state.selectedId);
    if (oldPath && state.selectedId !== newId) {
      state.deletedPaths.add(oldPath);
      state.originalPaths.delete(state.selectedId);
      state.originalPaths.set(newId, oldPath);
    }

    const index = state.entries.findIndex((entry) => entry.id === state.selectedId);
    state.entries[index] = updated;
    state.selectedId = updated.id;
    fillEntryForm(updated);
    renderEntryList();

    if (showStatus) {
      setStatus(`Alteracoes locais aplicadas em '${updated.id}'.`, "ok");
    }
  }

  function buildMetadataPayload() {
    syncMetadataFromInputs();
    return {
      catalogName: state.metadata.catalogName,
      channel: state.metadata.channel,
      schemaVersion: state.metadata.schemaVersion,
      catalogRevision: state.metadata.catalogRevision,
      defaults: {
        author: state.metadata.defaults?.author || "M.I.L.",
        detailsUrl: state.metadata.defaults?.detailsUrl || "https://miltraducoes.com/",
        language: state.metadata.defaults?.language || "pt-BR",
      },
    };
  }

  async function putFile(path, contentObject, message) {
    const body = {
      message,
      branch: state.config.branch,
      content: encodeContent(JSON.stringify(contentObject, null, 2) + "\n"),
    };
    const existingSha = state.fileShas.get(path);
    if (existingSha) {
      body.sha = existingSha;
    }
    const response = await githubFetch(repoApi(path), {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const payload = await response.json();
    state.fileShas.set(path, payload.content.sha);
    return payload;
  }

  async function deleteFile(path, message) {
    const sha = state.fileShas.get(path);
    if (!sha) {
      return;
    }
    await githubFetch(repoApi(path), {
      method: "DELETE",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        message,
        branch: state.config.branch,
        sha,
      }),
    });
    state.fileShas.delete(path);
  }

  async function publishAll() {
    syncConfigFromInputs();
    saveConfigToStorage();

    if (!state.config.owner || !state.config.repo || !state.config.branch || !state.config.token) {
      throw new Error("Preencha owner, repositório, branch e token antes de publicar.");
    }

    saveCurrentEntry(false);
    const metadataPayload = buildMetadataPayload();

    const usedIds = new Set();
    for (const entry of state.entries) {
      validateEntry(entry);
      if (usedIds.has(entry.id)) {
        throw new Error(`ID duplicado no catálogo: ${entry.id}`);
      }
      usedIds.add(entry.id);
    }

    setStatus("Publicando alterações no GitHub...", "");

    await putFile(METADATA_PATH, metadataPayload, "catalog: update metadata");

    for (const entry of state.entries) {
      const newPath = entryPath(entry);
      const originalPath = state.originalPaths.get(entry.id);
      if (originalPath && originalPath !== newPath) {
        state.deletedPaths.add(originalPath);
      }
      await putFile(newPath, entry, `catalog: upsert ${entry.id}`);
      state.originalPaths.set(entry.id, newPath);
    }

    for (const path of Array.from(state.deletedPaths)) {
      await deleteFile(path, `catalog: remove ${path.split("/").pop().replace(".json", "")}`);
      state.deletedPaths.delete(path);
    }

    setStatus("Publicação concluída. O workflow do Pages deve regenerar o índice automaticamente.", "ok");
  }

  async function loadRepository() {
    syncConfigFromInputs();
    saveConfigToStorage();

    if (!state.config.owner || !state.config.repo || !state.config.branch || !state.config.token) {
      throw new Error("Preencha owner, repositório, branch e token.");
    }

    setStatus("Carregando catálogo do GitHub...");
    const metadataFile = await fetchJsonFile(METADATA_PATH);
    const entryFiles = await fetchEntries();
    const generatedEntries = await fetchGeneratedIndex();

    state.metadata = metadataFile.content;
    state.entries = entryFiles.map((item) => item.content);
    state.originalPaths.clear();
    state.derivedEntries.clear();
    state.deletedPaths.clear();
    for (const item of entryFiles) {
      state.originalPaths.set(item.content.id, item.path);
    }
    for (const entry of generatedEntries) {
      if (entry && entry.id) {
        state.derivedEntries.set(entry.id, entry);
      }
    }
    state.selectedId = state.entries[0]?.id || null;
    state.isLoaded = true;

    syncInputsFromMetadata();
    fillEntryForm(findSelectedEntry());
    renderEntryList();
    setStatus(`Catálogo carregado com ${state.entries.length} entradas.`, "ok");
  }

  function createEmptyEntry() {
    const baseId = slugify(prompt("ID base do novo item:", "novo-item") || "novo-item") || "novo-item";
    let candidate = baseId;
    let suffix = 2;
    while (state.entries.some((entry) => entry.id === candidate)) {
      candidate = `${baseId}-${suffix++}`;
    }

    return {
      id: candidate,
      section: "translations",
      titleId: "",
      name: "",
      introPtBr: "",
      introEnUs: "",
      summary: "",
      summaryPtBr: "",
      summaryEnUs: "",
      author: state.metadata.defaults?.author || "M.I.L.",
      language: state.metadata.defaults?.language || "pt-BR",
      detailsUrl: state.metadata.defaults?.detailsUrl || "https://miltraducoes.com/",
      tags: [],
      compatibility: {},
      featured: false,
    };
  }

  function addEntry(duplicate = false) {
    const current = findSelectedEntry();
    const newEntry = duplicate && current ? structuredClone(current) : createEmptyEntry();
    if (duplicate && current) {
      let candidate = `${current.id}-copy`;
      let suffix = 2;
      while (state.entries.some((entry) => entry.id === candidate)) {
        candidate = `${current.id}-copy-${suffix++}`;
      }
      newEntry.id = candidate;
      newEntry.name = `${current.name} (copy)`;
    }
    state.entries.push(newEntry);
    state.selectedId = newEntry.id;
    fillEntryForm(newEntry);
    renderEntryList();
    setStatus(`Nova entrada pronta para edição: '${newEntry.id}'.`);
  }

  function deleteSelectedEntry() {
    const current = findSelectedEntry();
    if (!current) {
      return;
    }
    const confirmed = window.confirm(`Remover a entrada '${current.name || current.id}'?`);
    if (!confirmed) {
      return;
    }
    const originalPath = state.originalPaths.get(current.id);
    if (originalPath) {
      state.deletedPaths.add(originalPath);
      state.originalPaths.delete(current.id);
    }
    state.entries = state.entries.filter((entry) => entry.id !== current.id);
    state.selectedId = state.entries[0]?.id || null;
    fillEntryForm(findSelectedEntry());
    renderEntryList();
    setStatus(`Entrada '${current.id}' removida localmente. Publique para concluir.`, "ok");
  }

  function wireEvents() {
    els.loadRepo.addEventListener("click", async () => {
      try {
        await loadRepository();
      } catch (error) {
        console.error(error);
        setStatus(error.message, "error");
      }
    });

    els.clearSession.addEventListener("click", () => {
      localStorage.removeItem(STORAGE_KEY);
      state.config = { owner: "", repo: "", branch: "main", token: "" };
      syncInputsFromConfig();
      setStatus("Sessão limpa.");
    });

    els.search.addEventListener("input", renderEntryList);
    els.newEntry.addEventListener("click", () => addEntry(false));
    els.duplicateEntry.addEventListener("click", () => addEntry(true));
    els.saveEntry.addEventListener("click", () => {
      try {
        saveCurrentEntry(true);
      } catch (error) {
        console.error(error);
        setStatus(error.message, "error");
      }
    });
    els.deleteEntry.addEventListener("click", deleteSelectedEntry);
    els.publishAll.addEventListener("click", async () => {
      try {
        await publishAll();
      } catch (error) {
        console.error(error);
        setStatus(error.message, "error");
      }
    });
  }

  loadConfigFromStorage();
  syncInputsFromConfig();
  syncInputsFromMetadata();
  wireEvents();
  setStatus("Preencha os dados do repositório e carregue o catálogo.");
})();
