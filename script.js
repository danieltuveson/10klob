
let introScreen = true;
let helpScreen = false;
let quoteScreen = false;

addEventListener('load', async function(event) {
    updateCode();
    updateStdout();
    forceTerminalFocus();
});

// let codeInterval = setInterval(updateCode, 1000);
// let stdoutInterval = setInterval(updateStdout, 1000);

async function updateCode() {
    let code = document.querySelector('#code > pre');
    const response = await fetch("./code", {
        method: "GET",
    });
    code.textContent = await decodeChunks(response);
}

async function updateStdout() {
    let stdout = document.querySelector('#stdout > pre');
    const response = await fetch("./stdout", {
        method: "GET",
    });
    let output = await decodeChunks(response);
    console.log(output);
    stdout.textContent = output;
}

addEventListener('onclick', function (e) {
    forceTerminalFocus();
});

let showCursor = true;
let cursor = document.getElementById('cursor');

function toggleCursor(e) {
    if (showCursor) {
        cursor.removeAttribute('hidden');
    } else {
        cursor.setAttribute('hidden', 'true');
    }
    showCursor = !showCursor;
}

let cursorInterval = setInterval(toggleCursor, 500);

addEventListener('keypress', async function(event) {
    if (introScreen || helpScreen || quoteScreen) {
        event.preventDefault();
        document.getElementById('intro').setAttribute('hidden', 'true');
        introScreen = false;
        document.getElementById('help').setAttribute('hidden', 'true');
        helpScreen = false;
        document.getElementById('quote').setAttribute('hidden', 'true');
        quoteScreen = false;
        return;
    }
    let cursor = document.getElementById('cursor');
    cursor.removeAttribute('hidden');
    clearInterval(cursorInterval);
    cursorInterval = setInterval(toggleCursor, 500);

    if (event.key.toLowerCase() === 'enter') {
        event.preventDefault();
        handleCommand();
    }
    forceTerminalFocus();
});

async function handleCommand() {
    let elt = document.getElementById('stdin');
    let text = elt.innerHTML;
    console.log(text);
    elt.innerHTML = '';

    // In-browser commands
    let cleanText = text.trim().toLowerCase();
    if (cleanText === 'help') {
        let help = document.getElementById('help');
        help.removeAttribute('hidden');
        helpScreen = true;
    } else if (cleanText === 'quote') {
        let quote = document.getElementById('quote');
        quote.removeAttribute('hidden');
        quoteScreen = true;
    } else if (cleanText === 'run') {
        let response = await fetch("./run", { method: "POST", });
        console.log(await decodeChunks(response));
        await updateStdout();
    } else {
        const response = await fetch("./code", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded",
            },
            // Don't want to lower case this, in case there's a string in it
            body: new URLSearchParams({ "line": text.trim() }),
        });
        console.log(await decodeChunks(response));
        await updateCode();
    }
}

function forceTerminalFocus() {
    let elt = document.getElementById('stdin');
    elt.focus();
}

async function decodeChunks(response) {
    let lines = [];
    let decoder = new TextDecoder();
    let decoded;
    for await (const chunk of response.body) {
        decoded = decoder.decode(chunk);
        console.log(decoded);
        lines.push(decoded);
    }
    return lines.join('');
}

