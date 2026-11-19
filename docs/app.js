(() => {
  "use strict";

  const STORAGE_KEY = "ugasurvey.pages.v1";
  const ADMIN_USERNAME = "admin";
  const ADMIN_PASSWORD = "2739";
  const SURVEY_INDEX_URL = "surveys/index.json";
  const MAX_QUESTIONS = 40;
  const MAX_CHOICES = 24;
  const MAX_IMAGE_BYTES = 1.5 * 1024 * 1024;
  const TYPES = [
    { id: 0, name: "단답형", help: "짧은 답을 한 줄로 받아요. 이름, 지역처럼 간단한 질문에 잘 맞아요." },
    { id: 1, name: "장문형", help: "의견이나 경험처럼 긴 답변을 받아요." },
    { id: 2, name: "객관식", help: "응답자가 선택지 중 하나만 고를 수 있어요." },
    { id: 3, name: "체크박스", help: "응답자가 해당하는 답을 여러 개 고를 수 있어요." },
    { id: 4, name: "드롭다운", help: "선택지가 많을 때 목록에서 하나를 고르게 해요." },
    { id: 5, name: "선형 배율", help: "만족도나 동의 정도처럼 단계별로 평가를 받아요. 숫자를 선택지로 입력하세요." },
    { id: 6, name: "별점", help: "1~5 중 점수를 선택해요. 별점에 표시할 숫자를 선택지로 입력할 수 있어요." },
    { id: 7, name: "날짜", help: "응답자가 날짜를 입력해요." },
    { id: 8, name: "시간", help: "응답자가 시간을 입력해요." },
    { id: 9, name: "숫자 입력", help: "응답자가 숫자로 답해요." },
    { id: 10, name: "순위형", help: "순위를 매길 항목을 한 줄에 하나씩 입력하고, 응답자가 순서를 적어요." },
    { id: 11, name: "그리드 객관식", help: "첫 줄에 열, 다음 줄부터 행을 입력하세요. 각 줄의 항목은 | 로 나눠요." },
    { id: 12, name: "그리드 체크박스", help: "첫 줄에 열, 다음 줄부터 행을 입력하세요. 각 줄의 항목은 | 로 나눠요." },
    { id: 13, name: "이미지 선택형", help: "질문 이미지를 보여주고 선택지에서 답을 골라요." }
  ];
  const CHOICE_TYPES = new Set([2, 3, 4, 5, 6, 11, 12, 13]);
  const $ = (selector, root = document) => root.querySelector(selector);
  const state = {
    surveys: [],
    activeId: "",
    selectedQuestionId: "",
    saveTimer: 0,
    toastTimer: 0,
    previewFocused: false,
    isAdmin: false,
    editorInitialized: false,
    publishedSurveys: [],
    pendingSurveys: [],
    manifestLoaded: false
  };

  function makeId() {
    if (window.crypto && typeof window.crypto.randomUUID === "function") return window.crypto.randomUUID();
    return `q-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 9)}`;
  }

  function escapeHtml(value) {
    return String(value ?? "").replace(/[&<>"']/g, (char) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
    })[char]);
  }

  function activeSurvey() {
    return state.surveys.find((survey) => survey.id === state.activeId) || null;
  }

  function activeQuestion() {
    const survey = activeSurvey();
    return survey?.questions.find((question) => question.id === state.selectedQuestionId) || null;
  }

  function createQuestion(type = 0) {
    return {
      id: makeId(),
      title: "",
      description: "",
      options: type === 2 || type === 3 || type === 4 || type === 13 ? "선택지 1\n선택지 2" : "",
      type,
      required: false,
      imageData: "",
      imageName: ""
    };
  }

  function createSurvey(title = "새 설문") {
    return { id: makeId(), title, description: "", questions: [], updatedAt: new Date().toISOString() };
  }

  function normalizeSurvey(input) {
    const source = Array.isArray(input?.surveys) ? input.surveys[0] : input;
    if (!source || typeof source !== "object" || !Array.isArray(source.questions)) {
      throw new Error("설문과 질문이 들어 있는 JSON 파일을 선택해 주세요.");
    }
    return {
      id: typeof source.id === "string" && source.id ? source.id : makeId(),
      title: typeof source.title === "string" ? source.title : "가져온 설문",
      description: typeof source.description === "string" ? source.description : "",
      updatedAt: new Date().toISOString(),
      questions: source.questions.slice(0, MAX_QUESTIONS).map((question) => {
        const type = Number.isInteger(Number(question.type)) && Number(question.type) >= 0 && Number(question.type) < TYPES.length
          ? Number(question.type) : 0;
        const image = typeof question.imageData === "string" && question.imageData.startsWith("data:image/") ? question.imageData
          : typeof question.image === "string" && question.image.startsWith("data:image/") ? question.image : "";
        return {
          id: typeof question.id === "string" ? question.id : makeId(),
          title: typeof question.title === "string" ? question.title : "",
          description: typeof question.description === "string" ? question.description : "",
          options: Array.isArray(question.options) ? question.options.join("\n")
            : typeof question.options === "string" ? question.options : "",
          type,
          required: question.required === true || Number(question.required) === 1,
          imageData: image,
          imageName: typeof question.imageName === "string" ? question.imageName : ""
        };
      })
    };
  }

  function loadStore() {
    try {
      const saved = localStorage.getItem(STORAGE_KEY);
      if (!saved) return false;
      const parsed = JSON.parse(saved);
      if (!Array.isArray(parsed.surveys)) throw new Error("저장 형식이 올바르지 않습니다.");
      state.surveys = parsed.surveys.map(normalizeSurvey);
      state.activeId = state.surveys.some((survey) => survey.id === parsed.activeId)
        ? parsed.activeId : state.surveys[0]?.id || "";
      return state.surveys.length > 0;
    } catch (error) {
      console.error("Could not load saved UgaSurvey data.", error);
      showToast("이 브라우저의 저장 데이터를 읽지 못했습니다. JSON 백업을 불러와 주세요.");
      return false;
    }
  }

  function saveStore() {
    const survey = activeSurvey();
    if (survey) survey.updatedAt = new Date().toISOString();
    const status = $("#save-status");
    status.textContent = "저장 중…";
    status.classList.add("is-saving");
    status.classList.remove("is-error");
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({
        activeId: state.activeId,
        surveys: state.surveys
      }));
      status.textContent = "이 기기에 자동 저장";
      status.classList.remove("is-saving", "is-error");
      renderSurveySwitcher();
    } catch (error) {
      console.error("Could not save UgaSurvey data.", error);
      status.textContent = "저장 공간 부족";
      status.classList.remove("is-saving");
      status.classList.add("is-error");
      showToast("자동 저장에 실패했어요. 이미지 용량을 줄이거나 JSON 백업을 다운로드해 주세요.");
    }
  }

  function scheduleSave() {
    $("#save-status").textContent = "변경사항 저장 중…";
    $("#save-status").classList.add("is-saving");
    clearTimeout(state.saveTimer);
    state.saveTimer = window.setTimeout(saveStore, 250);
  }

  function showToast(message) {
    const toast = $("#toast");
    toast.textContent = message;
    toast.classList.add("is-visible");
    clearTimeout(state.toastTimer);
    state.toastTimer = window.setTimeout(() => toast.classList.remove("is-visible"), 3200);
  }

  function renderSurveySwitcher() {
    const select = $("#survey-switch");
    select.replaceChildren();
    state.surveys.forEach((survey) => {
      const option = document.createElement("option");
      option.value = survey.id;
      option.textContent = survey.title.trim() || "제목 없는 설문";
      option.selected = survey.id === state.activeId;
      select.append(option);
    });
  }

  function questionLabel(question, index) {
    return question.title.trim() || `새 질문 ${index + 1}`;
  }

  function renderQuestionList() {
    const survey = activeSurvey();
    const list = $("#question-list");
    list.replaceChildren();
    $("#question-count").textContent = survey.questions.length
      ? `${survey.questions.length}개 질문 · 위에서부터 응답`
      : "아직 질문이 없어요";
    survey.questions.forEach((question, index) => {
      const row = document.createElement("div");
      row.className = `question-item${question.id === state.selectedQuestionId ? " is-selected" : ""}`;
      const select = document.createElement("button");
      select.className = "question-select";
      select.type = "button";
      select.dataset.action = "select-question";
      select.dataset.questionId = question.id;
      select.setAttribute("aria-current", question.id === state.selectedQuestionId ? "true" : "false");
      select.innerHTML = `<strong>${escapeHtml(`${String(index + 1).padStart(2, "0")}. ${questionLabel(question, index)}`)}</strong><small>${escapeHtml(TYPES[question.type]?.name || TYPES[0].name)}${question.required ? " · 필수" : ""}</small>`;
      const order = document.createElement("div");
      order.className = "question-order";
      order.innerHTML = `<button class="mini-icon" type="button" data-action="move-up" data-question-id="${escapeHtml(question.id)}" aria-label="질문 ${index + 1} 위로 이동" title="위로 이동" ${index === 0 ? "disabled" : ""}>↑</button><button class="mini-icon" type="button" data-action="move-down" data-question-id="${escapeHtml(question.id)}" aria-label="질문 ${index + 1} 아래로 이동" title="아래로 이동" ${index === survey.questions.length - 1 ? "disabled" : ""}>↓</button>`;
      row.append(select, order);
      list.append(row);
    });
  }

  function renderChoiceEditor(question) {
    const choices = question.options.split(/\r?\n/);
    const list = $("#choice-list");
    list.replaceChildren();
    choices.forEach((choice, index) => {
      const row = document.createElement("div");
      row.className = "choice-row";
      const bullet = document.createElement("span");
      bullet.className = "choice-bullet";
      bullet.textContent = question.type === 2 ? "○" : question.type === 3 || question.type === 12 || question.type === 13 ? "□" : "•";
      bullet.setAttribute("aria-hidden", "true");
      const input = document.createElement("input");
      input.className = "text-input choice-input";
      input.type = "text";
      input.maxLength = 180;
      input.value = choice;
      input.id = `choice-${index}`;
      input.dataset.choiceIndex = String(index);
      input.setAttribute("aria-label", `선택지 ${index + 1}`);
      const tools = document.createElement("div");
      tools.className = "choice-order";
      tools.innerHTML = `<button class="mini-icon" type="button" data-action="choice-up" data-choice-index="${index}" aria-label="선택지 ${index + 1} 위로 이동" title="위로 이동" ${index === 0 ? "disabled" : ""}>↑</button><button class="mini-icon" type="button" data-action="choice-down" data-choice-index="${index}" aria-label="선택지 ${index + 1} 아래로 이동" title="아래로 이동" ${index === choices.length - 1 ? "disabled" : ""}>↓</button><button class="mini-icon" type="button" data-action="remove-choice" data-choice-index="${index}" aria-label="선택지 ${index + 1} 삭제" title="선택지 삭제">×</button>`;
      row.append(bullet, input, tools);
      list.append(row);
    });
    $("#add-choice").disabled = choices.length >= MAX_CHOICES;
  }

  function renderEditor() {
    const survey = activeSurvey();
    const question = activeQuestion();
    $("#survey-title").value = survey.title;
    $("#survey-description").value = survey.description;
    $("#empty-editor").hidden = Boolean(question);
    $("#question-editor").hidden = !question;
    if (!question) {
      $("#editor-heading").textContent = "첫 질문을 추가해 볼까요?";
      return;
    }

    const index = survey.questions.findIndex((item) => item.id === question.id);
    $("#editor-heading").textContent = `질문 ${index + 1} · 내용을 입력해 주세요`;
    $("#current-question-number").textContent = `질문 ${index + 1}`;
    $("#question-title").value = question.title;
    $("#question-description").value = question.description;
    $("#question-required").checked = question.required;
    const typeSelect = $("#question-type");
    typeSelect.replaceChildren(...TYPES.map((type) => {
      const option = document.createElement("option");
      option.value = String(type.id);
      option.textContent = type.name;
      option.selected = type.id === question.type;
      return option;
    }));
    $("#type-help").textContent = TYPES[question.type].help;
    $("#choice-editor").hidden = !CHOICE_TYPES.has(question.type);
    if (CHOICE_TYPES.has(question.type)) renderChoiceEditor(question);
    $("#image-empty").hidden = Boolean(question.imageData);
    $("#question-image-preview").hidden = !question.imageData;
    $("#question-image-preview").src = question.imageData || "";
    $("#remove-image").hidden = !question.imageData;
    $("#question-image").value = "";
    renderQuestionList();
    renderPreview();
  }

  function optionsFor(question) {
    const values = question.options.split(/\r?\n/).map((value) => value.trim()).filter(Boolean);
    if (values.length) return values;
    if (question.type === 5 || question.type === 6) return ["1", "2", "3", "4", "5"];
    return [];
  }

  function renderQuestionInput(question, index, mode) {
    const prefix = `${mode}-${escapeHtml(question.id)}`;
    const name = `${mode}-${escapeHtml(question.id)}`;
    const required = question.required ? "required" : "";
    const options = optionsFor(question);
    if ([2, 3, 13].includes(question.type)) {
      const inputType = question.type === 2 ? "radio" : "checkbox";
      return `<div class="response-options">${options.map((option, choiceIndex) =>
        `<label class="response-option"><input type="${inputType}" name="${name}" value="${escapeHtml(option)}" ${question.required && (inputType === "radio" || choiceIndex === 0) ? "required" : ""}><span>${escapeHtml(option || `선택지 ${choiceIndex + 1}`)}</span></label>`
      ).join("")}</div>`;
    }
    if (question.type === 4) {
      return `<select class="response-field" name="${name}" ${required}><option value="">선택해 주세요</option>${options.map((option) => `<option value="${escapeHtml(option)}">${escapeHtml(option)}</option>`).join("")}</select>`;
    }
    if (question.type === 5 || question.type === 6) {
      return `<div class="response-scale" role="group" aria-label="${escapeHtml(question.title)}">${options.map((option) =>
        `<label class="scale-choice"><input type="radio" name="${name}" value="${escapeHtml(option)}" ${required}><span>${escapeHtml(question.type === 6 ? `★ ${option}` : option)}</span></label>`
      ).join("")}</div>`;
    }
    if (question.type === 10) {
      return `<ol class="response-rank">${options.map((option) => `<li>${escapeHtml(option)}</li>`).join("")}</ol><input class="response-field" name="${name}" placeholder="원하는 순서를 적어 주세요." ${required}>`;
    }
    if (question.type === 11 || question.type === 12) {
      const lines = question.options.split(/\r?\n/).filter((line) => line.trim());
      const columns = (lines[0] || "").split("|").map((value) => value.trim()).filter(Boolean);
      const rows = lines.slice(1).map((line) => line.trim()).filter(Boolean);
      const inputType = question.type === 11 ? "radio" : "checkbox";
      if (!columns.length || !rows.length) return `<p class="grid-help">편집 화면에서 첫 줄에 열, 다음 줄에 행을 입력하세요. | 로 나누면 표가 만들어집니다.</p>`;
      return `<div class="grid-scroll"><table class="grid-preview"><thead><tr><th></th>${columns.map((column) => `<th>${escapeHtml(column)}</th>`).join("")}</tr></thead><tbody>${rows.map((row, rowIndex) => `<tr><th>${escapeHtml(row)}</th>${columns.map((column, columnIndex) => `<td><input type="${inputType}" name="${name}-${rowIndex}" value="${escapeHtml(column)}" aria-label="${escapeHtml(`${row} ${column}`)}" ${question.required && (inputType === "radio" || columnIndex === 0) ? "required" : ""}></td>`).join("")}</tr>`).join("")}</tbody></table></div>`;
    }
    if (question.type === 7) return `<input class="response-field" type="date" name="${name}" ${required}>`;
    if (question.type === 8) return `<input class="response-field" type="time" name="${name}" ${required}>`;
    if (question.type === 9) return `<input class="response-field" type="number" name="${name}" placeholder="숫자를 입력해 주세요." ${required}>`;
    if (question.type === 1) return `<textarea class="response-field response-long" name="${name}" placeholder="답변을 입력해 주세요." ${required}></textarea>`;
    return `<input class="response-field" type="text" name="${name}" placeholder="답변을 입력해 주세요." ${required}>`;
  }

  function renderPreview() {
    const survey = activeSurvey();
    const preview = $("#live-preview");
    const questions = survey.questions;
    if (!questions.length) {
      preview.innerHTML = `<div class="preview-empty"><span aria-hidden="true">▤</span><p>질문을 추가하면 응답자에게 보이는 모습이 여기에 나타나요.</p></div>`;
      return;
    }
    preview.innerHTML = `<section class="response-card"><h3>${escapeHtml(survey.title.trim() || "설문 제목")}</h3>${survey.description.trim() ? `<p class="response-description">${escapeHtml(survey.description)}</p>` : ""}${questions.map((question, index) => `<article class="response-question"><h4>${escapeHtml(question.title.trim() || `질문 ${index + 1}`)}${question.required ? '<span class="response-required"> *</span>' : ""}</h4>${question.imageData ? `<img class="response-question-image" src="${escapeHtml(question.imageData)}" alt="${escapeHtml(question.imageName || question.title || "질문 이미지")}">` : ""}${question.description.trim() ? `<p class="response-description">${escapeHtml(question.description)}</p>` : ""}${renderQuestionInput(question, index, "preview")}</article>`).join("")}<button class="preview-submit" type="button" data-action="preview-submit">응답 제출 미리보기</button></section>`;
  }

  function addQuestion(type = 0) {
    if (!state.isAdmin) return;
    const survey = activeSurvey();
    if (survey.questions.length >= MAX_QUESTIONS) {
      showToast(`질문은 최대 ${MAX_QUESTIONS}개까지 추가할 수 있어요.`);
      return;
    }
    const question = createQuestion(type);
    survey.questions.push(question);
    state.selectedQuestionId = question.id;
    renderEditor();
    scheduleSave();
    $("#question-title").focus();
    $("#question-title").select();
  }

  function questionFromTarget(target) {
    const id = target.dataset.questionId;
    const survey = activeSurvey();
    return { survey, index: survey.questions.findIndex((question) => question.id === id) };
  }

  function moveQuestion(id, direction) {
    if (!state.isAdmin) return;
    const survey = activeSurvey();
    const index = survey.questions.findIndex((question) => question.id === id);
    const target = index + direction;
    if (index < 0 || target < 0 || target >= survey.questions.length) return;
    [survey.questions[index], survey.questions[target]] = [survey.questions[target], survey.questions[index]];
    state.selectedQuestionId = survey.questions[target].id;
    renderEditor();
    scheduleSave();
  }

  function duplicateQuestion() {
    if (!state.isAdmin) return;
    const survey = activeSurvey();
    const index = survey.questions.findIndex((question) => question.id === state.selectedQuestionId);
    if (survey.questions.length >= MAX_QUESTIONS) {
      showToast(`질문은 최대 ${MAX_QUESTIONS}개까지 추가할 수 있어요.`);
      return;
    }
    const copy = { ...survey.questions[index], id: makeId(), title: `${survey.questions[index].title} (복사)` };
    survey.questions.splice(index + 1, 0, copy);
    state.selectedQuestionId = copy.id;
    renderEditor();
    scheduleSave();
    showToast("질문을 복제했어요.");
  }

  function deleteQuestion() {
    if (!state.isAdmin) return;
    const survey = activeSurvey();
    const index = survey.questions.findIndex((question) => question.id === state.selectedQuestionId);
    if (index < 0) return;
    survey.questions.splice(index, 1);
    state.selectedQuestionId = survey.questions[Math.min(index, survey.questions.length - 1)]?.id || "";
    renderEditor();
    scheduleSave();
    showToast("질문을 삭제했어요.");
  }

  function updateSurveyField(field, value) {
    if (!state.isAdmin) return;
    const survey = activeSurvey();
    survey[field] = value;
    renderSurveySwitcher();
    renderQuestionList();
    renderPreview();
    scheduleSave();
  }

  function updateQuestionField(field, value) {
    if (!state.isAdmin) return;
    const question = activeQuestion();
    if (!question) return;
    question[field] = value;
    if (field === "type") {
      if (CHOICE_TYPES.has(value) && !question.options.trim()) {
        question.options = value === 5 || value === 6 ? "1\n2\n3\n4\n5"
          : value === 11 || value === 12 ? "좋음 | 보통 | 아쉬움\n질문 1\n질문 2"
            : "선택지 1\n선택지 2";
      }
      renderEditor();
    } else {
      if (field === "title" || field === "required" || field === "options") renderQuestionList();
      if (field === "imageData") renderEditor();
      else renderPreview();
    }
    scheduleSave();
  }

  function updateChoice(index, value) {
    if (!state.isAdmin) return;
    const question = activeQuestion();
    const choices = question.options.split(/\r?\n/);
    if (index < 0 || index >= choices.length) return;
    choices[index] = value;
    question.options = choices.join("\n");
    renderPreview();
    scheduleSave();
  }

  function addChoice() {
    if (!state.isAdmin) return;
    const question = activeQuestion();
    const choices = question.options.split(/\r?\n/);
    if (choices.length >= MAX_CHOICES) {
      showToast(`선택지는 최대 ${MAX_CHOICES}개까지 추가할 수 있어요.`);
      return;
    }
    choices.push("");
    question.options = choices.join("\n");
    renderChoiceEditor(question);
    renderPreview();
    scheduleSave();
    const inputs = $$("#choice-list input");
    inputs[inputs.length - 1]?.focus();
  }

  function $$(selector, root = document) {
    return [...root.querySelectorAll(selector)];
  }

  function changeChoice(index, action) {
    if (!state.isAdmin) return;
    const question = activeQuestion();
    const choices = question.options.split(/\r?\n/);
    if (action === "remove-choice") {
      if (choices.length <= 1) {
        showToast("선택지는 하나 이상 남겨 주세요.");
        return;
      }
      choices.splice(index, 1);
    } else {
      const direction = action === "choice-up" ? -1 : 1;
      const target = index + direction;
      if (target < 0 || target >= choices.length) return;
      [choices[index], choices[target]] = [choices[target], choices[index]];
    }
    question.options = choices.join("\n");
    renderChoiceEditor(question);
    renderPreview();
    scheduleSave();
  }

  function handleImageUpload(file) {
    if (!state.isAdmin) return;
    const question = activeQuestion();
    if (!question || !file) return;
    if (!["image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp"].includes(file.type)) {
      showToast("PNG, JPG, GIF, WebP, BMP 이미지 파일을 선택해 주세요.");
      return;
    }
    if (file.size > MAX_IMAGE_BYTES) {
      showToast("이미지는 1.5MB 이하로 선택해 주세요. 큰 이미지는 먼저 크기를 줄여 주세요.");
      return;
    }
    const reader = new FileReader();
    reader.onerror = () => showToast("이미지를 읽지 못했습니다. 다른 파일을 선택해 주세요.");
    reader.onload = () => {
      if (typeof reader.result !== "string") {
        showToast("이미지 파일 형식을 확인해 주세요.");
        return;
      }
      question.imageData = reader.result;
      question.imageName = file.name;
      renderEditor();
      scheduleSave();
      showToast("질문 이미지를 첨부했어요.");
    };
    reader.readAsDataURL(file);
  }

  function downloadFile(name, content, type) {
    const blob = new Blob([content], { type });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = name;
    document.body.append(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function surveyJson(survey) {
    return {
      version: 1,
      surveys: [{
        title: survey.title,
        description: survey.description,
        responseCount: 0,
        questions: survey.questions.map((question) => ({
          id: question.id,
          title: question.title,
          description: question.description,
          options: question.options,
          image: "",
          imageName: question.imageName,
          imageData: question.imageData,
          type: question.type,
          required: question.required ? 1 : 0,
          answers: []
        }))
      }]
    };
  }

  function filename(title, extension) {
    const safe = title.normalize("NFKC").replace(/[<>:"/\\|?*\u0000-\u001f]/g, "").trim().replace(/\s+/g, "-");
    return `${safe || "ugasurvey"}${extension}`;
  }

  function showPublicView() {
    state.isAdmin = false;
    $("#builder-view").hidden = true;
    $("#public-view").hidden = false;
    $("#login-panel").hidden = true;
    $("#login-form").reset();
    $("#login-error").hidden = true;
    renderPublicSurveys();
  }

  function showBuilderView() {
    state.isAdmin = true;
    $("#public-view").hidden = true;
    $("#builder-view").hidden = false;
    if (!state.editorInitialized) {
      const saved = loadStore();
      if (!saved) {
        const survey = createSurvey();
        state.surveys = [survey];
        state.activeId = survey.id;
      }
      state.selectedQuestionId = activeSurvey()?.questions[0]?.id || "";
      state.editorInitialized = true;
      if (!saved) saveStore();
    }
    renderAll();
  }

  function renderPublicSurveys(errorMessage = "") {
    const list = $("#public-surveys");
    const status = $("#public-survey-status");
    list.replaceChildren();

    if (errorMessage) {
      status.textContent = errorMessage;
      return;
    }

    status.textContent = state.publishedSurveys.length
      ? `${state.publishedSurveys.length}개의 설문에 참여할 수 있어요.`
      : "아직 공개된 설문이 없습니다.";

    if (!state.publishedSurveys.length) {
      const empty = document.createElement("div");
      empty.className = "public-empty";
      const icon = document.createElement("span");
      icon.setAttribute("aria-hidden", "true");
      icon.textContent = "☷";
      const message = document.createElement("p");
      message.textContent = "새로 공개된 설문이 여기에 표시됩니다.";
      empty.append(icon, message);
      list.append(empty);
      return;
    }

    state.publishedSurveys.forEach((survey) => {
      const card = document.createElement("article");
      card.className = "public-survey-card";
      const title = document.createElement("h3");
      title.textContent = survey.title;
      const description = document.createElement("p");
      description.textContent = survey.description || "설문에 참여해 주세요.";
      const link = document.createElement("a");
      link.href = `surveys/${survey.file}`;
      link.textContent = "설문 참여하기";
      link.setAttribute("aria-label", `${survey.title} 설문 참여하기`);
      card.append(title, description, link);
      list.append(card);
    });
  }

  function validPublishedSurvey(entry) {
    return entry && typeof entry.file === "string" && /^\d{3,}\.html$/.test(entry.file)
      && typeof entry.title === "string" && entry.title.trim();
  }

  async function loadPublishedSurveys() {
    try {
      const response = await fetch(SURVEY_INDEX_URL, { cache: "no-store" });
      if (!response.ok) throw new Error(`공개 설문 목록을 읽지 못했습니다. (${response.status})`);
      const manifest = await response.json();
      if (!manifest || !Array.isArray(manifest.surveys)) throw new Error("공개 설문 목록 형식이 올바르지 않습니다.");
      state.publishedSurveys = manifest.surveys.filter(validPublishedSurvey).map((survey) => ({
        file: survey.file,
        title: survey.title.trim(),
        description: typeof survey.description === "string" ? survey.description.trim() : ""
      }));
      state.manifestLoaded = true;
      renderPublicSurveys();
    } catch (error) {
      console.error("Could not load the published survey list.", error);
      renderPublicSurveys("설문 목록을 불러오지 못했습니다. GitHub Pages 주소에서 다시 열어 주세요.");
    }
  }

  function nextSurveyFile() {
    const numbers = [...state.publishedSurveys, ...state.pendingSurveys.map((item) => item.entry)]
      .map((survey) => Number(survey.file.match(/^(\d+)\.html$/)?.[1] || 0));
    return `${String(Math.max(0, ...numbers) + 1).padStart(3, "0")}.html`;
  }

  function exportJson() {
    if (!state.isAdmin) return;
    const survey = activeSurvey();
    downloadFile(filename(survey.title, ".json"), JSON.stringify(surveyJson(survey), null, 2), "application/json;charset=utf-8");
    showToast("JSON 백업 파일을 다운로드했어요.");
  }

  function respondentHtml(survey) {
    const embedded = JSON.stringify(surveyJson(survey)).replace(/</g, "\\u003c").replace(/\u2028/g, "\\u2028").replace(/\u2029/g, "\\u2029");
    return `<!doctype html>
<html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${escapeHtml(survey.title || "UgaSurvey 응답")}</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#f4f6fb;color:#1b2940;font:16px/1.65 Pretendard,"Malgun Gothic",sans-serif}
main{width:min(760px,100%);margin:38px auto;padding:20px}.card{padding:clamp(20px,5vw,36px);border:1px solid #e3e8f1;border-radius:20px;background:white;box-shadow:0 12px 40px #2231530b}
h1{margin:0;font-size:clamp(24px,5vw,34px);line-height:1.3}.description{margin:12px 0 24px;color:#68778e;white-space:pre-wrap}
.question{margin:15px 0;padding:18px;border:1px solid #e4e9f1;border-radius:13px}.question h2{margin:0;font-size:16px}.help{margin:7px 0 13px;color:#748198;font-size:13px;white-space:pre-wrap}.required{color:#d55656}
.q-image{display:block;max-width:100%;max-height:360px;margin:12px 0;border-radius:9px;object-fit:contain}
label.option{display:flex;align-items:center;gap:9px;margin:9px 0;color:#43516a}input,textarea,select{font:inherit}
input[type=text],input[type=number],input[type=date],input[type=time],select,textarea,.rank{width:100%;min-height:44px;padding:10px 12px;border:1px solid #d5dce8;border-radius:9px;background:white;color:#1b2940}
textarea{min-height:110px;resize:vertical}.scale{display:flex;flex-wrap:wrap;gap:8px}.scale label{padding:7px 10px;border:1px solid #dce2ed;border-radius:8px}
.grid-wrap{overflow:auto}.grid{width:100%;border-collapse:collapse}.grid th,.grid td{padding:10px 5px;border-bottom:1px solid #edf0f5;text-align:center}.grid th:first-child{text-align:left}
button.submit{width:100%;min-height:48px;margin-top:12px;border:0;border-radius:10px;background:#3d63e8;color:white;font:inherit;font-weight:700;cursor:pointer}
.notice{margin:15px 0 0;color:#8490a2;font-size:12px}.done{padding:35px 10px;text-align:center}.done h2{font-size:23px}
@media(max-width:600px){main{margin:12px auto;padding:10px}.question{padding:14px}.card{border-radius:15px}}
</style></head>
<body><main><section class="card"><h1 id="title"></h1><p id="description" class="description"></p><form id="form"></form>
<p class="notice">이 설문은 오프라인 응답용입니다. 제출하면 응답 JSON 파일이 이 기기에 다운로드되며, 제작자에게 자동 전송되지 않습니다.</p>
<div id="done" class="done" hidden><h2>응답을 저장했어요.</h2><p>다운로드된 응답 JSON을 설문 제작자에게 전달해 주세요.</p><button class="submit" id="again" type="button">다른 응답 작성하기</button></div>
</section></main><script id="survey-data" type="application/json">${embedded}</script>
<script>(function(){
const pack=JSON.parse(document.getElementById('survey-data').textContent);const s=pack.surveys[0],form=document.getElementById('form');
document.title=(s.title||'설문 응답')+' · UgaSurvey';document.getElementById('title').textContent=s.title||'새 설문';document.getElementById('description').textContent=s.description||'';
const choices=q=>(q.options||'').split(/\\r?\\n/).map(x=>x.trim()).filter(Boolean);
s.questions.forEach((q,i)=>{const box=document.createElement('section');box.className='question';box.dataset.q=String(i);const h=document.createElement('h2');h.textContent=(i+1)+'. '+(q.title||'질문')+(q.required?' *':'');box.append(h);
if(q.imageData){const img=document.createElement('img');img.className='q-image';img.src=q.imageData;img.alt=q.imageName||q.title||'질문 이미지';box.append(img)}
if(q.description){const p=document.createElement('p');p.className='help';p.textContent=q.description;box.append(p)}
const opts=choices(q);let el;
if([2,3,13].includes(q.type)){const wrap=document.createElement('div');opts.forEach((v,choiceIndex)=>{const label=document.createElement('label');label.className='option';const input=document.createElement('input');input.type=q.type===2?'radio':'checkbox';input.name='q'+i;input.value=v;input.required=!!q.required&&(q.type===2||choiceIndex===0);label.append(input,document.createTextNode(v));wrap.append(label)});box.append(wrap)}
else if(q.type===4){el=document.createElement('select');el.add(new Option('선택해 주세요',''));opts.forEach(v=>el.add(new Option(v,v)));el.required=!!q.required;el.name='q'+i;box.append(el)}
else if(q.type===5||q.type===6){const wrap=document.createElement('div');wrap.className='scale';(opts.length?opts:['1','2','3','4','5']).forEach(v=>{const label=document.createElement('label');const input=document.createElement('input');input.type='radio';input.name='q'+i;input.value=v;input.required=!!q.required;label.append(input,document.createTextNode(q.type===6?' ★ '+v:' '+v));wrap.append(label)});box.append(wrap)}
else if(q.type===10){const list=document.createElement('ol');list.className='rank';opts.forEach(v=>{const li=document.createElement('li');li.textContent=v;list.append(li)});box.append(list);el=document.createElement('input');el.type='text';el.name='q'+i;el.required=!!q.required;el.placeholder='원하는 순서를 적어 주세요';box.append(el)}
else if(q.type===11||q.type===12){const lines=(q.options||'').split(/\\r?\\n/).filter(Boolean),cols=(lines[0]||'').split('|').map(x=>x.trim()).filter(Boolean),rows=lines.slice(1).map(x=>x.trim()).filter(Boolean);
if(cols.length&&rows.length){const wrap=document.createElement('div');wrap.className='grid-wrap';const table=document.createElement('table');table.className='grid';const head=document.createElement('tr');head.append(document.createElement('th'));cols.forEach(c=>{const th=document.createElement('th');th.textContent=c;head.append(th)});const thead=document.createElement('thead');thead.append(head);table.append(thead);const body=document.createElement('tbody');rows.forEach((r,ri)=>{const tr=document.createElement('tr'),th=document.createElement('th');th.textContent=r;tr.append(th);cols.forEach(c=>{const td=document.createElement('td'),input=document.createElement('input');input.type=q.type===11?'radio':'checkbox';input.name='q'+i+'-'+ri;input.value=c;input.required=!!q.required;input.setAttribute('aria-label',r+' '+c);td.append(input);tr.append(td)});body.append(tr)});table.append(body);wrap.append(table);box.append(wrap)}else{const p=document.createElement('p');p.className='help';p.textContent='표 설정이 비어 있습니다.';box.append(p)}}
else{el=document.createElement(q.type===1?'textarea':'input');if(el.tagName==='INPUT'){el.type=q.type===7?'date':q.type===8?'time':q.type===9?'number':'text'}el.name='q'+i;el.required=!!q.required;el.placeholder='답변을 입력해 주세요';box.append(el)}
form.append(box)});
const submit=document.createElement('button');submit.className='submit';submit.type='submit';submit.textContent='응답 제출';form.append(submit);
form.addEventListener('submit',event=>{event.preventDefault();if(!form.reportValidity())return;if(!confirm('응답을 제출할까요? 응답은 이 기기에 JSON 파일로 저장됩니다.'))return;
const answers=s.questions.map((q,i)=>{const box=form.querySelector('[data-q="'+i+'"]');const selected=[...box.querySelectorAll('input:checked')].map(x=>x.value);const field=box.querySelector('input:not([type=radio]):not([type=checkbox]),textarea,select');return {question:q.title,answer:selected.length?(q.type===2?selected[0]:selected):(field?field.value:'')}});
const result={survey:s.title,submittedAt:new Date().toISOString(),answers};const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(result,null,2)],{type:'application/json'}));a.download='ugasurvey-response.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);form.hidden=true;document.querySelector('.notice').hidden=true;document.getElementById('done').hidden=false});
document.getElementById('again').addEventListener('click',()=>{form.reset();form.hidden=false;document.querySelector('.notice').hidden=false;document.getElementById('done').hidden=true});
})();</script></body></html>`;
  }

  function exportHtml() {
    if (!state.isAdmin) return;
    const survey = activeSurvey();
    if (!survey.title.trim()) {
      $("#survey-title").focus();
      showToast("먼저 설문 제목을 입력해 주세요.");
      return;
    }
    if (!survey.questions.length) {
      showToast("응답용 HTML을 만들려면 질문을 하나 이상 추가해 주세요.");
      return;
    }
    if (!state.manifestLoaded) {
      showToast("공개 목록을 읽지 못했어요. GitHub Pages에서 새로고침한 뒤 다시 내보내 주세요.");
      return;
    }
    const pending = state.pendingSurveys.find((item) => item.surveyId === survey.id);
    const file = pending?.entry.file || nextSurveyFile();
    const entry = {
      file,
      title: survey.title.trim(),
      description: survey.description.trim()
    };
    if (pending) pending.entry = entry;
    else state.pendingSurveys.push({ surveyId: survey.id, entry });
    const html = respondentHtml(survey);
    downloadFile(file, html, "text/html;charset=utf-8");
    $("#publish-followup").hidden = false;
    showToast(`${file}을 만들었어요. docs/surveys에 넣고 기본 브랜치에 푸시하면 목록에 자동으로 올라가요.`);
  }

  function importJson(file) {
    if (!state.isAdmin || !file) return;
    const reader = new FileReader();
    reader.onerror = () => showToast("JSON 파일을 읽지 못했습니다.");
    reader.onload = () => {
      try {
        const parsed = JSON.parse(String(reader.result));
        const sources = Array.isArray(parsed?.surveys) ? parsed.surveys : [parsed];
        if (!sources.length) throw new Error("JSON 파일에 설문이 없습니다.");
        const imported = sources.map((source) => normalizeSurvey(source));
        const existing = state.surveys.findIndex((survey) => survey.id === state.activeId);
        if (existing < 0) state.surveys.push(...imported);
        else state.surveys.splice(existing + 1, 0, ...imported);
        state.activeId = imported[0].id;
        state.selectedQuestionId = imported[0].questions[0]?.id || "";
        renderAll();
        saveStore();
        showToast("JSON 설문을 불러왔어요.");
      } catch (error) {
        console.error("Could not import survey JSON.", error);
        showToast(error instanceof SyntaxError ? "JSON 문법을 확인해 주세요." : error.message);
      }
      $("#import-file").value = "";
    };
    reader.readAsText(file);
  }

  function loadExample() {
    if (!state.isAdmin) return;
    const example = createSurvey("동아리 활동 만족도 조사");
    example.description = "더 즐거운 활동을 만들기 위해 의견을 듣고 있어요. 약 2분이 걸립니다.";
    example.questions = [
      Object.assign(createQuestion(2), {
        title: "가장 참여하기 편한 활동 시간은 언제인가요?",
        description: "하나를 선택해 주세요.",
        options: "평일 오후\n평일 저녁\n주말 오전\n주말 오후",
        required: true
      }),
      Object.assign(createQuestion(1), {
        title: "앞으로 해보고 싶은 활동이 있나요?",
        description: "떠오르는 아이디어를 편하게 적어 주세요."
      }),
      Object.assign(createQuestion(5), {
        title: "최근 활동에 전반적으로 만족하셨나요?",
        options: "1\n2\n3\n4\n5",
        required: true
      })
    ];
    const currentIndex = state.surveys.findIndex((survey) => survey.id === state.activeId);
    if (currentIndex < 0) state.surveys.push(example);
    else state.surveys.splice(currentIndex + 1, 0, example);
    state.activeId = example.id;
    state.selectedQuestionId = example.questions[0].id;
    renderAll();
    saveStore();
    showToast("예시 설문을 열었어요. 내용을 자유롭게 바꿔 보세요.");
  }

  function newSurvey() {
    if (!state.isAdmin) return;
    if (!window.confirm("새 설문을 만들까요? 지금 설문은 자동 저장되어 나중에 다시 열 수 있어요.")) return;
    const survey = createSurvey();
    state.surveys.push(survey);
    state.activeId = survey.id;
    state.selectedQuestionId = "";
    state.previewFocused = false;
    $("#workspace").classList.remove("preview-focused");
    renderAll();
    saveStore();
    $("#survey-title").focus();
  }

  function renderAll() {
    renderSurveySwitcher();
    renderEditor();
  }

  function togglePreviewFocus() {
    if (!state.isAdmin) return;
    state.previewFocused = !state.previewFocused;
    $("#workspace").classList.toggle("preview-focused", state.previewFocused);
    $("#toggle-preview").textContent = state.previewFocused ? "편집 화면으로 돌아가기" : "응답 화면 크게 보기";
  }

  function handleClick(event) {
    const target = event.target.closest("[data-action]");
    if (!target || !state.isAdmin) return;
    const { action } = target.dataset;
    if (action === "add-question") addQuestion();
    else if (action === "select-question") {
      state.selectedQuestionId = target.dataset.questionId;
      renderEditor();
    } else if (action === "move-up" || action === "move-down") {
      const id = target.dataset.questionId || state.selectedQuestionId;
      moveQuestion(id, action === "move-up" ? -1 : 1);
    } else if (action === "duplicate-question") duplicateQuestion();
    else if (action === "delete-question") {
      if (window.confirm("이 질문을 삭제할까요?")) deleteQuestion();
    } else if (action === "remove-choice" || action === "choice-up" || action === "choice-down") {
      changeChoice(Number(target.dataset.choiceIndex), action);
    } else if (action === "preview-submit") {
      const preview = $("#live-preview");
      const fields = $$("input[required], select[required], textarea[required]", preview);
      const firstEmpty = fields.find((field) => field.type === "radio" || field.type === "checkbox"
        ? !$$(`input[name="${CSS.escape(field.name)}"]:checked`, preview).length
        : !field.value);
      if (firstEmpty) showToast("미리보기에서는 응답을 저장하지 않아요. 필수 답변은 응답용 HTML에서 확인해 주세요.");
      else showToast("미리보기가 정상적으로 입력됐어요. 응답은 저장되지 않았습니다.");
    }
  }

  function bindEvents() {
    $("#show-login").addEventListener("click", () => {
      $("#login-panel").hidden = false;
      $("#login-error").hidden = true;
      $("#login-username").focus();
    });
    $("#close-login").addEventListener("click", () => {
      $("#login-panel").hidden = true;
      $("#login-error").hidden = true;
    });
    $("#login-form").addEventListener("submit", (event) => {
      event.preventDefault();
      const username = $("#login-username").value.trim();
      const password = $("#login-password").value;
      if (username !== ADMIN_USERNAME || password !== ADMIN_PASSWORD) {
        $("#login-error").hidden = false;
        $("#login-password").value = "";
        $("#login-password").focus();
        return;
      }
      $("#login-error").hidden = true;
      showBuilderView();
    });
    $("#logout").addEventListener("click", showPublicView);
    document.addEventListener("click", handleClick);
    $("#add-question").addEventListener("click", () => addQuestion());
    $("#new-survey").addEventListener("click", newSurvey);
    $("#load-example").addEventListener("click", loadExample);
    $("#survey-switch").addEventListener("change", (event) => {
      if (!state.isAdmin) return;
      state.activeId = event.target.value;
      state.selectedQuestionId = activeSurvey()?.questions[0]?.id || "";
      renderEditor();
      saveStore();
    });
    $("#survey-title").addEventListener("input", (event) => updateSurveyField("title", event.target.value));
    $("#survey-description").addEventListener("input", (event) => updateSurveyField("description", event.target.value));
    $("#question-title").addEventListener("input", (event) => updateQuestionField("title", event.target.value));
    $("#question-description").addEventListener("input", (event) => updateQuestionField("description", event.target.value));
    $("#question-type").addEventListener("change", (event) => updateQuestionField("type", Number(event.target.value)));
    $("#question-required").addEventListener("change", (event) => updateQuestionField("required", event.target.checked));
    $("#choice-list").addEventListener("input", (event) => {
      if (event.target.matches("[data-choice-index]")) updateChoice(Number(event.target.dataset.choiceIndex), event.target.value);
    });
    $("#add-choice").addEventListener("click", addChoice);
    $("#question-image").addEventListener("change", (event) => handleImageUpload(event.target.files?.[0]));
    $("#remove-image").addEventListener("click", () => {
      if (!state.isAdmin) return;
      const question = activeQuestion();
      question.imageData = "";
      question.imageName = "";
      renderEditor();
      scheduleSave();
      showToast("질문 이미지를 제거했어요.");
    });
    $("#download-json").addEventListener("click", exportJson);
    $("#download-html").addEventListener("click", exportHtml);
    $("#import-json").addEventListener("click", () => $("#import-file").click());
    $("#import-file").addEventListener("change", (event) => importJson(event.target.files?.[0]));
    $("#toggle-preview").addEventListener("click", togglePreviewFocus);
    window.addEventListener("beforeunload", () => {
      clearTimeout(state.saveTimer);
      if (state.isAdmin) saveStore();
    });
  }

  function init() {
    bindEvents();
    loadPublishedSurveys();
  }

  init();
})();
