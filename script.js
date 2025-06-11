"use strict";

let introScreen = true;
let helpScreen = false;
let quoteScreen = false;

let showCursor = true;
let cursor = document.getElementById("cursor");

let cursorInterval = setInterval(toggleCursor, 500);
// let codeInterval = setInterval(updateCode, 1000);
// let stdoutInterval = setInterval(updateStdout, 1000);

async function updateCode(lineno) {
    let code = document.querySelector("#code");
    console.log("lineno:", lineno);
    const response = await fetch(`./code?lineno=${lineno}`, { method: "GET" });
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
addEventListener("click", function (e) {
    hideSplashpages();
    forceTerminalFocus();
});

addEventListener("load", async function(event) {
    await updateCode(1);
    await updateStdout();
    forceTerminalFocus();
});

// TODO: Make it so fake cursor follows real one if user uses arrow keys to navigate
function toggleCursor(e) {
    if (showCursor) {
        cursor.removeAttribute("hidden");
    } else {
        cursor.setAttribute("hidden", "true");
    }
    showCursor = !showCursor;
}

addEventListener("keypress", async function(event) {
    if (introScreen || helpScreen || quoteScreen) {
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
    introScreen = false;
    document.getElementById("help").setAttribute("hidden", "true");
    helpScreen = false;
    document.getElementById("quote").setAttribute("hidden", "true");
    quoteScreen = false;
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
        helpScreen = true;
    } else if (cleanText === "quote") {
        let quote = document.getElementById("quote");
        quote.removeAttribute("hidden");
        quoteScreen = true;
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
        await updateCode(lineno);
    }
}

async function decodeChunks(response) {
    let lines = [];
    let decoder = new TextDecoder();
    let decoded;
    for await (const chunk of response.body) {
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
