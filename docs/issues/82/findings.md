# Findings

## Report

The port runs well overall on the reporter's Windows PC.
The reporter plays with the keyboard and an external pad does not register as player two.
That blocks local multiplayer and leaves only one playable character.
Story progress stops when the game freezes and closes at World 1's big castle.
The crash happens on entering the cannon that launches the player toward the airship boss.
The reporter attached a screenshot of the error at cannon entry.
They also tried a second controller for two-player play and changed settings without success.
The game still sees only one player.
They hope to beat the airship boss and finish the game on Europe 1.3.0.
They filed the report on version v0.9.1 (f99ee83).
The app wrote crash logs on 2026-11-20.

## Status

GitHub marks issue #82 OPEN with the `bug` label.
Author MrTekaDo opened it on 2026-11-21.
There are no issue comments.

## Cannon crash

The steps match the World 1 castle cannon that sends the player to the airship boss.
`CHANGELOG.md` version **0.10.1** (2026-11-24) fixes that scene.
The entry states the game no longer closes when you take that cannon (#82).
The reporter used v0.9.1, which predates that release.

## Second controller

The reporter also reports that a second controller is not recognized for two-player play.
That matches open issue #68, where a second pad drives player one instead of player two.
It also matches open issue #76, where a second controller is not detected on Steam Deck.
`CHANGELOG.md` cites no fix for second-player input in version 0.10.1.

## Assessment

The cannon crash appears addressed in v0.10.1.
The second-controller report remains a separate open multiplayer input issue.