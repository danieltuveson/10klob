"use strict";

let REFRESH_RATE = 1000;
let SLEEP_WHEN   = 20000;

let isSplashscreen = true;
let currentLine = 1;

let cursor = document.getElementById("cursor");

let cursorInterval = setInterval(toggleCursor, 500);

let codeInterval;
let stdoutInterval;
let sleepTimeout;

function setRefresh() {
    codeInterval = setInterval(updateCode, REFRESH_RATE);
    stdoutInterval = setInterval(updateStdout, REFRESH_RATE);
}

// If idle for too long, stop spamming web server
function gotoSleep() {
    if (!isSplashscreen) {
        let sleep = document.querySelector("#sleep");
        sleep.removeAttribute("hidden");
    }
    clearInterval(codeInterval);
    clearInterval(stdoutInterval);
    clearTimeout(sleepTimeout);

    sleepTimeout = null;
    codeInterval = null;
    stdoutInterval = null;
    isSplashscreen = true;
}

async function updateCode() {
    let code = document.querySelector("#code");
    console.log("currentLine:", currentLine);
    const response = await fetch(`./code?lineno=${currentLine}`, { method: "GET" });
    let output = await decodeChunks(response);
    if (response.ok) {
        writeOutputToDiv(code, output);
    } else {
        console.log(output);
    }
}

async function updateStdout() {
    let stdout = document.querySelector("#stdout");
    const response = await fetch("./stdout", { method: "GET" });
    let output = await decodeChunks(response);
    if (response.ok) {
        writeOutputToDiv(stdout, output);
    } else {
        console.log(output);
    }
}

function writeOutputToDiv(node, output) {
    node.innerHTML = "";
    output.split("\n").forEach(function (line) {
        let lineNode = document.createElement("pre");
        lineNode.textContent = line;
        node.appendChild(lineNode);
    });
}

function wakeUp() {
    clearTimeout(sleepTimeout);
    sleepTimeout = setTimeout(gotoSleep, SLEEP_WHEN);
    if (!codeInterval) {
        setRefresh();
    }
}

addEventListener("click", function (e) {
    wakeUp();
    hideSplashpages();
    forceTerminalFocus();
});

addEventListener("load", async function(event) {
    await updateCode();
    await updateStdout();
    forceTerminalFocus();
    setRefresh();
    clearTimeout(sleepTimeout);
    sleepTimeout = setTimeout(gotoSleep, SLEEP_WHEN);
});

// TODO: Make it so fake cursor follows real one if user uses arrow keys to navigate
function toggleCursor(e) {
    // Instead of using "hidden" we have to use this stupid hack to make it work on android
    cursor.classList.toggle("hide");
}

addEventListener("keypress", async function(event) {
    wakeUp();
    if (isSplashscreen) {
        event.preventDefault();
        hideSplashpages();
        return;
    }
    cursor.removeAttribute("hidden");
    clearInterval(cursorInterval);
    cursorInterval = setInterval(toggleCursor, 500);

    let key = event.key.toLowerCase();
    if (key === "enter") {
        event.preventDefault();
        handleCommand();
    }
    forceTerminalFocus();
});

function hideSplashpages() {
    document.getElementById("intro").setAttribute("hidden", "true");
    document.getElementById("help").setAttribute("hidden", "true");
    document.getElementById("quote").setAttribute("hidden", "true");
    document.getElementById("sleep").setAttribute("hidden", "true");
    isSplashscreen = false;
}

async function handleCommand() {
    let elt = document.getElementById("stdin");
    let text = decodeHtml(elt.innerHTML);
    console.log(text);
    elt.innerHTML = "";

    // In-browser commands
    let cleanText = text.trim().toLowerCase();
    let lineno = parseInt(text);
    console.log(lineno);
    if (cleanText === "help") {
        let help = document.getElementById("help");
        help.removeAttribute("hidden");
        isSplashscreen = true;
    } else if (cleanText === "quote") {
        let quote = document.getElementById("quote");
        quote.removeAttribute("hidden");
        isSplashscreen = true;
    } else if (cleanText === "run") {
        let response = await fetch("./run", { method: "POST" });
        console.log(await decodeChunks(response));
        await updateStdout();
    } else if (!isNaN(lineno)) {
        const response = await fetch("./code", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded"
            },
            // Don't want to lower case text, in case there"s a string in it
            body: new URLSearchParams({ "line": text })
        });
        await decodeChunks(response);

        if (lineno < 15) {
            lineno = 1;
        } else {
            // Want to make sure to display lines above, if they exist, that way the user has
            // context for the surrounding code
            lineno -= 8;
        }
        currentLine = lineno;
        await updateCode();
    }
}

async function* streamAsyncIterable(stream) {
  const reader = stream.getReader();
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) return;
      yield value;
    }
  } finally {
    reader.releaseLock();
  }
}

async function decodeChunks(response) {
    let lines = [];
    let decoder = new TextDecoder();
    let decoded;
    for await (const chunk of streamAsyncIterable(response.body)) {
        decoded = decoder.decode(chunk);
        lines.push(decoded);
    }
    let output = lines.join("");

    let i = 0;
    for (let line of output.split("\n")) {
        for (let j = 0; j < line.length; j++) {
            // if (line[j]
        }
        if (i < 10) {
            console.log(line);
        } else {
            console.log("...");
            break;
        }
        i++;
    }
    return lines.join("");
}

function forceTerminalFocus() {
    let elt = document.getElementById("stdin");
    elt.focus();
}

function decodeHtml(html) {
    var txt = document.createElement("textarea");
    txt.innerHTML = html;
    return txt.value;
}
