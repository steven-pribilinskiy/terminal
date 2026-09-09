#!/usr/bin/env node
// Stop hook: refuse to end a turn that promises a later report when nothing is
// actually watching for the thing being reported on.
//
// Why this exists: a CI build here takes ~40 minutes, so "I'll report when it
// lands" is a useful thing to say - and on 2026-09-09 it was said with no watch
// started, twice. Once the watch was started against a run id that had been
// guessed rather than looked up, so it watched nothing and reported nothing.
// Both times the promise was the only thing the user had, and it was empty.
//
// The check is a process probe rather than a flag the assistant sets, because a
// flag would be trusting the same judgement that failed.
//
// Exit 0 = allow. Exit 2 = block, and stderr goes back to the model.

import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';

const PROMISE = [
  /\bi(?:'| w)?ll (?:report|tell you|let you know|update you|come back|follow up|ping you)\b/i,
  /\breport (?:back )?(?:when|once|as soon as)\b/i,
  /\blet you know (?:when|once|as soon as)\b/i,
  /\bwhen (?:it|the (?:run|build|ci))\s+(?:lands|finishes|completes|is green|reports)\b/i,
  /\bi'?m watching\b|\bwatching (?:it|the (?:run|build|ci))\b/i,
];

// Phrases that mean the opposite - the turn is explicitly handing the wait back
// to the user - so a promise-shaped sentence near them is not a promise.
const DISCLAIMED = [
  /\byou'?ll (?:need to|have to)\b/i,
  /\brun it yourself\b/i,
  /\btell me when\b/i,
];

function lastAssistantText(transcriptPath) {
  let raw;
  try {
    raw = readFileSync(transcriptPath, 'utf8');
  } catch {
    return '';
  }
  const lines = raw.split('\n').filter(Boolean);
  for (let i = lines.length - 1; i >= 0; i--) {
    let entry;
    try {
      entry = JSON.parse(lines[i]);
    } catch {
      continue;
    }
    if (entry?.type !== 'assistant') continue;
    const content = entry?.message?.content;
    if (!Array.isArray(content)) continue;
    const text = content
      .filter((b) => b?.type === 'text' && typeof b.text === 'string')
      .map((b) => b.text)
      .join('\n')
      .trim();
    if (text) return text;
  }
  return '';
}

// Is anything actually waiting on CI? `gh run watch` blocks until the run ends,
// so its presence is the evidence. Win32_Process is the only reliable way to see
// a command line on Windows - wmic is gone on 11, and git-bash `ps` does not
// carry arguments for native processes.
function watcherRunning() {
  let out;
  try {
    out = execFileSync(
      'powershell.exe',
      [
        '-NoProfile',
        '-NonInteractive',
        '-Command',
        // Single quotes only, and no -Filter. The obvious spelling,
        // -Filter "Name='gh.exe'", carries double quotes INSIDE the argument;
        // Windows argument quoting mangles them on the way through
        // execFileSync, PowerShell then prints nothing, and the count parses as
        // NaN. That is not a loud failure - it silently reads as "no watcher"
        // and blocks a turn that did everything right. Caught by testing the
        // allow case, which is the case that is easy not to test.
        "(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'gh.exe' -and $_.CommandLine -like '*run*watch*' } | Measure-Object).Count",
      ],
      { encoding: 'utf8', timeout: 15000 },
    );
  } catch {
    return true; // probe broke - fail OPEN, see below
  }

  const trimmed = String(out).trim();
  if (!/^\d+$/.test(trimmed)) {
    // Anything that is not a plain integer means the probe did not work.
    // Fail OPEN: a hook that blocks because its own check broke is a worse
    // problem than the one it guards against, and it is indistinguishable from
    // the real thing to whoever is on the other end.
    return true;
  }
  return Number(trimmed) > 0;
}

let input = '';
try {
  input = readFileSync(0, 'utf8');
} catch {
  process.exit(0);
}

let payload = {};
try {
  payload = JSON.parse(input || '{}');
} catch {
  process.exit(0);
}

// Already blocked once this turn: let it through rather than loop forever.
if (payload.stop_hook_active) process.exit(0);

const text = lastAssistantText(payload.transcript_path ?? '');
if (!text) process.exit(0);

// Only the closing part of the message makes a promise about what happens next.
const tail = text.slice(-1200);
if (!PROMISE.some((re) => re.test(tail))) process.exit(0);
if (DISCLAIMED.some((re) => re.test(tail))) process.exit(0);
if (watcherRunning()) process.exit(0);

process.stderr.write(
  [
    'You told the user you would report back, but no `gh run watch` is running, so nothing will bring the result back and the promise is empty.',
    '',
    'Do one of these before stopping:',
    '  1. Start the watch, with a run id you LOOKED UP rather than guessed:',
    '       gh run list --repo <owner>/<repo> --limit 5 --json databaseId,headSha,workflowName,status',
    '       gh run watch <id> --repo <owner>/<repo> --exit-status   (run_in_background: true)',
    '  2. Or remove the claim and say plainly that the user should check, or that you will report only if asked.',
    '',
    'Do not restate the promise without doing one of those.',
  ].join('\n'),
);
process.exit(2);
