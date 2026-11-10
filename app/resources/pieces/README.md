# Piece sets

SVG piece sets shipped with Kiebitz, taken unchanged from the Lichess repository
(https://github.com/lichess-org/lila, `public/piece/<set>/`), where their licences are listed in
`COPYING.md`. All are compatible with Kiebitz's GPL-3.0 licence.

| Set | Author | Licence |
| --- | --- | --- |
| cburnett (default) | Colin M.L. Burnett | GPLv2 or later |
| merida | Armando Hernandez Marroquin | GPLv2 or later |
| chessnut | Alexis Luengas (https://github.com/LexLuengas/chessnut-pieces) | Apache License 2.0 |
| fantasy | Maurizio Monge (https://github.com/maurimo/chess-art) | MIT |

File names: `w`/`b` for the colour, then `K Q R B N P` for the piece (`wK.svg` = white king).

To add a set: copy its 12 files into a new folder here, add it to the table with its author and
licence (it must be GPL-compatible: no "non-commercial" licences), and add its name to
`PieceSet::available()` in `app/board/PieceSet.cpp`.
