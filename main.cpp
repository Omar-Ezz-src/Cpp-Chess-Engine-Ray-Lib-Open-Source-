#include "raylib.h"
#include "raymath.h"
#include<iostream>
#include <map>
#include <string>
#include <vector>
using namespace std;

//------------------helpers

#define NOT_A_FILE  0xFEFEFEFEFEFEFEFEULL
#define NOT_H_FILE  0x7F7F7F7F7F7F7F7FULL

uint64_t squareBit(int square) { return 1ULL << square; }

int bitSquare(uint64_t bit) {
    static const int table[64] = {
         0,  1, 56,  2, 57, 49, 28,  3, 61, 58, 42, 50, 38, 29, 17,  4,
        62, 47, 59, 36, 45, 43, 51, 22, 53, 39, 33, 30, 24, 18, 12,  5,
        63, 55, 48, 27, 60, 41, 37, 16, 46, 35, 44, 21, 52, 32, 23, 11,
        54, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19,  9, 13,  8,  7,  6
    };
    return table[(bit * 0x03f79d71b4ca8b09ULL) >> 58];
}

int square(int file, int rank) { return rank * 8 + file; }

bool validateFEN(const string& fen) {
    vector<string> fields;
    string token;
    for (char c : fen) {
        if (c == ' ') { fields.push_back(token); token.clear(); }
        else token += c;
    }
    fields.push_back(token);

    if (fields.size() != 6) {
        cout << "FEN Error: Expected 6 fields, got " << fields.size() << "\n";
        return false;
    }

    vector<string> ranks;
    string rank;
    for (char c : fields[0]) {
        if (c == '/') { ranks.push_back(rank); rank.clear(); }
        else rank += c;
    }
    ranks.push_back(rank);

    if (ranks.size() != 8) {
        cout << "FEN Error: Expected 8 ranks, got " << ranks.size() << "\n";
        return false;
    }

    int whiteKings = 0, blackKings = 0;
    for (int i = 0; i < 8; i++) {
        int count = 0;
        for (char c : ranks[i]) {
            if (isdigit(c)) count += (c - '0');
            else if (string("PNBRQKpnbrqk").find(c) != string::npos) {
                count++;
                if (c == 'K') whiteKings++;
                if (c == 'k') blackKings++;
            }
            else {
                cout << "FEN Error: Invalid character '" << c << "' in rank " << (8 - i) << "\n";
                return false;
            }
        }
        if (count != 8) {
            cout << "FEN Error: Rank " << (8 - i) << " has " << count << " squares, expected 8\n";
            return false;
        }
    }

    if (whiteKings != 1) { cout << "FEN Error: Expected 1 white king, found " << whiteKings << "\n"; return false; }
    if (blackKings != 1) { cout << "FEN Error: Expected 1 black king, found " << blackKings << "\n"; return false; }

    if (fields[1] != "w" && fields[1] != "b") {
        cout << "FEN Error: Side to move must be 'w' or 'b', got '" << fields[1] << "'\n";
        return false;
    }

    if (fields[2] != "-") {
        for (char c : fields[2]) {
            if (string("KQkq").find(c) == string::npos) {
                cout << "FEN Error: Invalid castling character '" << c << "'\n";
                return false;
            }
        }
    }

    if (fields[3] != "-") {
        if (fields[3].size() != 2
            || fields[3][0] < 'a' || fields[3][0] > 'h'
            || (fields[3][1] != '3' && fields[3][1] != '6')) {
            cout << "FEN Error: Invalid en passant square '" << fields[3] << "'\n";
            return false;
        }
    }

    for (char c : fields[4]) {
        if (!isdigit(c)) {
            cout << "FEN Error: Halfmove clock must be a number, got '" << fields[4] << "'\n";
            return false;
        }
    }

    for (char c : fields[5]) {
        if (!isdigit(c)) {
            cout << "FEN Error: Fullmove number must be a number, got '" << fields[5] << "'\n";
            return false;
        }
    }
    if (stoi(fields[5]) < 1) {
        cout << "FEN Error: Fullmove number must be >= 1\n";
        return false;
    }

    return true;
}

//------------------ define

enum Piece : uint8_t { R, N, B, Q, K, P, r, n, b, q, k, p, None };

struct Move {
    int from = -1, to = -1;
    Piece piece = None;
    Piece captured = None;
    bool wasCastleWK = false;
    bool wasCastleWQ = false;
    bool wasCastleBK = false;
    bool wasCastleBQ = false;
};

class Board {
public:
    uint64_t bitboards[12] = { 0 };
    bool sideToMove = true; // true = white, false = black

    bool castleWK = false;
    bool castleWQ = false;
    bool castleBK = false;
    bool castleBQ = false;

    uint64_t whitePieces() {
        return bitboards[P] | bitboards[N] | bitboards[B]
            | bitboards[R] | bitboards[Q] | bitboards[K];
    }
    uint64_t blackPieces() {
        return bitboards[p] | bitboards[n] | bitboards[b]
            | bitboards[r] | bitboards[q] | bitboards[k];
    }
    uint64_t occupied() { return whitePieces() | blackPieces(); }

    Vector2 highlightedSquare = { 0, 0 };
    Piece   heldPieceType = None;
    int     heldSquare = -1;
};

map<Piece, Texture2D> textures = {};
int squareEdgeSide = 64;
int margin = 32;

Color backGroundColor = { 79, 32, 13, 255 };
Color boardDarkColor = BROWN;
Color boardLightColor = LIGHTGRAY;
Color availableMoveColor = { 10, 200, 50, 150 };

const uint64_t RANK_3 = 0x0000000000FF0000ULL;
const uint64_t RANK_6 = 0x0000FF0000000000ULL;

//------------------logic functions

void parseFEN(const std::string& fen, Board& board) {
    int rank = 7, file = 0;
    char lastChar = '.';
    for (char c : fen) {
        if (c == ' ') { lastChar = c; break; }
        if (lastChar == ' ') {
            switch (c) {
            case 'w': board.sideToMove = true;  break;
            case 'b': board.sideToMove = false; break;
            }
        }
        lastChar = c;

        if (c == '/') { rank--; file = 0; }
        else if (isdigit(c)) { file += (c - '0'); }
        else {
            Piece piece = None;
            switch (c) {
            case 'P': piece = P; break; case 'N': piece = N; break;
            case 'B': piece = B; break; case 'R': piece = R; break;
            case 'Q': piece = Q; break; case 'K': piece = K; break;
            case 'p': piece = p; break; case 'n': piece = n; break;
            case 'b': piece = b; break; case 'r': piece = r; break;
            case 'q': piece = q; break; case 'k': piece = k; break;
            }
            if (piece != None) {
                board.bitboards[piece] |= (1ULL << (rank * 8 + file));
                file++;
            }
        }
    }

    int spaces = 0;
    for (int i = 0; i < (int)fen.size(); i++) {
        if (fen[i] == ' ') spaces++;
        if (spaces == 2) {
            for (int j = i + 1; j < (int)fen.size() && fen[j] != ' '; j++) {
                if (fen[j] == 'K') board.castleWK = true;
                if (fen[j] == 'Q') board.castleWQ = true;
                if (fen[j] == 'k') board.castleBK = true;
                if (fen[j] == 'q') board.castleBQ = true;
            }
            break;
        }
    }
}

//------------------move generation

uint64_t getRookAttacks(uint64_t rook, uint64_t occupied, uint64_t friendly) {
    uint64_t result = 0, ray;

    ray = rook;
    while ((ray = (ray << 8))) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    ray = rook;
    while ((ray = (ray >> 8))) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    ray = rook;
    while ((ray = (ray << 1) & NOT_A_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    ray = rook;
    while ((ray = (ray >> 1) & NOT_H_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    return result;
}

uint64_t getValidRookMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t occupied = board->occupied();
    uint64_t result = 0;
    while (pieceBitboard) {
        uint64_t single = pieceBitboard & (0ULL - pieceBitboard);
        result |= getRookAttacks(single, occupied, friendly);
        pieceBitboard &= pieceBitboard - 1;
    }
    return result;
}

uint64_t getValidKnightMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t notAFile = NOT_A_FILE;
    uint64_t notHFile = NOT_H_FILE;
    uint64_t notABFile = notAFile & (notAFile << 1);
    uint64_t notGHFile = notHFile & (notHFile >> 1);
    uint64_t result = 0;

    result |= (pieceBitboard << 17) & notAFile;
    result |= (pieceBitboard << 15) & notHFile;
    result |= (pieceBitboard << 10) & notABFile;
    result |= (pieceBitboard << 6) & notGHFile;
    result |= (pieceBitboard >> 17) & notHFile;
    result |= (pieceBitboard >> 15) & notAFile;
    result |= (pieceBitboard >> 10) & notGHFile;
    result |= (pieceBitboard >> 6) & notABFile;

    return result & ~friendly;
}

uint64_t getBishopAttacks(uint64_t bishop, uint64_t occupied, uint64_t friendly) {
    uint64_t result = 0, ray;

    ray = bishop;
    while ((ray = (ray << 9) & NOT_A_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    ray = bishop;
    while ((ray = (ray << 7) & NOT_H_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    ray = bishop;
    while ((ray = (ray >> 7) & NOT_A_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    ray = bishop;
    while ((ray = (ray >> 9) & NOT_H_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }
    return result;
}

uint64_t getValidBishopMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t occupied = board->occupied();
    uint64_t result = 0;
    while (pieceBitboard) {
        uint64_t single = pieceBitboard & (0ULL - pieceBitboard);
        result |= getBishopAttacks(single, occupied, friendly);
        pieceBitboard &= pieceBitboard - 1;
    }
    return result;
}

uint64_t getValidQueenMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    return getValidRookMoves(pieceBitboard, board, isWhite)
        | getValidBishopMoves(pieceBitboard, board, isWhite);
}

uint64_t getValidPawnMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t against = isWhite ? board->blackPieces() : board->whitePieces();
    uint64_t empty = ~(friendly | against);
    uint64_t result = 0;

    if (isWhite) {
        uint64_t singlePush = (pieceBitboard << 8) & empty;
        uint64_t doublePush = ((singlePush & RANK_3) << 8) & empty;
        result |= singlePush | doublePush;
        result |= (pieceBitboard << 9) & NOT_A_FILE & against;
        result |= (pieceBitboard << 7) & NOT_H_FILE & against;
    }
    else {
        uint64_t singlePush = (pieceBitboard >> 8) & empty;
        uint64_t doublePush = ((singlePush & RANK_6) >> 8) & empty;
        result |= singlePush | doublePush;
        result |= (pieceBitboard >> 7) & NOT_A_FILE & against;
        result |= (pieceBitboard >> 9) & NOT_H_FILE & against;
    }
    return result;
}

bool isSquareAttacked(int sq, bool byWhite, Board* board) {
    uint64_t sqBit = squareBit(sq);

    uint64_t knights = byWhite ? board->bitboards[N] : board->bitboards[n];
    if (getValidKnightMoves(sqBit, board, !byWhite) & knights) return true;

    uint64_t bishops = byWhite ? (board->bitboards[B] | board->bitboards[Q])
        : (board->bitboards[b] | board->bitboards[q]);
    if (getValidBishopMoves(sqBit, board, !byWhite) & bishops) return true;

    uint64_t rooks = byWhite ? (board->bitboards[R] | board->bitboards[Q])
        : (board->bitboards[r] | board->bitboards[q]);
    if (getValidRookMoves(sqBit, board, !byWhite) & rooks) return true;

    uint64_t pawns = byWhite ? board->bitboards[P] : board->bitboards[p];
    if (byWhite) {
        if (((sqBit >> 9) & NOT_H_FILE & pawns) || ((sqBit >> 7) & NOT_A_FILE & pawns)) return true;
    }
    else {
        if (((sqBit << 9) & NOT_A_FILE & pawns) || ((sqBit << 7) & NOT_H_FILE & pawns)) return true;
    }

    uint64_t king = byWhite ? board->bitboards[K] : board->bitboards[k];
    uint64_t kingAttacks = 0;
    kingAttacks |= (sqBit << 8);
    kingAttacks |= (sqBit >> 8);
    kingAttacks |= (sqBit << 1) & NOT_A_FILE;
    kingAttacks |= (sqBit >> 1) & NOT_H_FILE;
    kingAttacks |= (sqBit << 9) & NOT_A_FILE;
    kingAttacks |= (sqBit << 7) & NOT_H_FILE;
    kingAttacks |= (sqBit >> 7) & NOT_A_FILE;
    kingAttacks |= (sqBit >> 9) & NOT_H_FILE;
    if (kingAttacks & king) return true;

    return false;
}

uint64_t getValidKingMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t occ = board->occupied();
    uint64_t result = 0;

    result |= (pieceBitboard << 8);
    result |= (pieceBitboard >> 8);
    result |= (pieceBitboard << 1) & NOT_A_FILE;
    result |= (pieceBitboard << 9) & NOT_A_FILE;
    result |= (pieceBitboard >> 7) & NOT_A_FILE;
    result |= (pieceBitboard >> 1) & NOT_H_FILE;
    result |= (pieceBitboard << 7) & NOT_H_FILE;
    result |= (pieceBitboard >> 9) & NOT_H_FILE;

    if (isWhite) {
        if (pieceBitboard & squareBit(4) && !isSquareAttacked(4, !isWhite, board)) {
            if (board->castleWK
                && !(occ & squareBit(5)) && !(occ & squareBit(6))
                && !isSquareAttacked(5, !isWhite, board)
                && !isSquareAttacked(6, !isWhite, board))
                result |= squareBit(6);

            if (board->castleWQ
                && !(occ & squareBit(3)) && !(occ & squareBit(2)) && !(occ & squareBit(1))
                && !isSquareAttacked(3, !isWhite, board)
                && !isSquareAttacked(2, !isWhite, board))
                result |= squareBit(2);
        }
    }
    else {
        if (pieceBitboard & squareBit(60) && !isSquareAttacked(60, !isWhite, board)) {
            if (board->castleBK
                && !(occ & squareBit(61)) && !(occ & squareBit(62))
                && !isSquareAttacked(61, !isWhite, board)
                && !isSquareAttacked(62, !isWhite, board))
                result |= squareBit(62);

            if (board->castleBQ
                && !(occ & squareBit(59)) && !(occ & squareBit(58)) && !(occ & squareBit(57))
                && !isSquareAttacked(59, !isWhite, board)
                && !isSquareAttacked(58, !isWhite, board))
                result |= squareBit(58);
        }
    }

    return result & ~friendly;
}

uint64_t getValidPieceMoves(Piece piece, uint64_t pieceBitboard, Board* board, bool isWhite) {
    if (piece == N || piece == n) return getValidKnightMoves(pieceBitboard, board, isWhite);
    if (piece == R || piece == r) return getValidRookMoves(pieceBitboard, board, isWhite);
    if (piece == B || piece == b) return getValidBishopMoves(pieceBitboard, board, isWhite);
    if (piece == Q || piece == q) return getValidQueenMoves(pieceBitboard, board, isWhite);
    if (piece == K || piece == k) return getValidKingMoves(pieceBitboard, board, isWhite);
    if (piece == P || piece == p) return getValidPawnMoves(pieceBitboard, board, isWhite);
    return 0;
}

//------------------make / unmake

void movePiece(uint64_t& pieceBitboard, int from, int to) {
    pieceBitboard &= ~squareBit(from);
    pieceBitboard |= squareBit(to);
}

Move buildMove(Board* board, Piece piece, int from, int to) {
    Move move;
    move.from = from;
    move.to = to;
    move.piece = piece;
    move.captured = None;
    move.wasCastleWK = board->castleWK;
    move.wasCastleWQ = board->castleWQ;
    move.wasCastleBK = board->castleBK;
    move.wasCastleBQ = board->castleBQ;

    // Detect captured piece (search enemy range)
    int start = (piece < 6) ? 6 : 0;
    int end = (piece < 6) ? 12 : 6;
    for (int i = start; i < end; i++) {
        if ((board->bitboards[i] >> to) & 1) {
            move.captured = static_cast<Piece>(i);
            break;
        }
    }
    return move;
}

void makeMove(Board* board, Move& move) {
    movePiece(board->bitboards[move.piece], move.from, move.to);

    // Remove captured piece
    if (move.captured != None)
        board->bitboards[move.captured] &= ~squareBit(move.to);

    // Move castling rook and revoke rights
    if (move.piece == K) {
        board->castleWK = board->castleWQ = false;
        if (move.from == 4 && move.to == 6) movePiece(board->bitboards[R], 7, 5);
        if (move.from == 4 && move.to == 2) movePiece(board->bitboards[R], 0, 3);
    }
    if (move.piece == k) {
        board->castleBK = board->castleBQ = false;
        if (move.from == 60 && move.to == 62) movePiece(board->bitboards[r], 63, 61);
        if (move.from == 60 && move.to == 58) movePiece(board->bitboards[r], 56, 59);
    }

    // Revoke castling rights if a rook moves from its starting square
    if (move.piece == R) {
        if (move.from == 0) board->castleWQ = false;
        if (move.from == 7) board->castleWK = false;
    }
    if (move.piece == r) {
        if (move.from == 56) board->castleBQ = false;
        if (move.from == 63) board->castleBK = false;
    }

    // Revoke castling rights if a rook is captured on its starting square
    if (move.captured == R) {
        if (move.to == 0) board->castleWQ = false;
        if (move.to == 7) board->castleWK = false;
    }
    if (move.captured == r) {
        if (move.to == 56) board->castleBQ = false;
        if (move.to == 63) board->castleBK = false;
    }

    board->sideToMove = !board->sideToMove;
}

void unmakeMove(Board* board, Move& move) {
    board->sideToMove = !board->sideToMove;

    movePiece(board->bitboards[move.piece], move.to, move.from);

    // Restore captured piece
    if (move.captured != None)
        board->bitboards[move.captured] |= squareBit(move.to);

    // Restore castling rook position
    if (move.piece == K) {
        if (move.from == 4 && move.to == 6) movePiece(board->bitboards[R], 5, 7);
        if (move.from == 4 && move.to == 2) movePiece(board->bitboards[R], 3, 0);
    }
    if (move.piece == k) {
        if (move.from == 60 && move.to == 62) movePiece(board->bitboards[r], 61, 63);
        if (move.from == 60 && move.to == 58) movePiece(board->bitboards[r], 59, 56);
    }

    // Restore all castling rights from the saved state
    board->castleWK = move.wasCastleWK;
    board->castleWQ = move.wasCastleWQ;
    board->castleBK = move.wasCastleBK;
    board->castleBQ = move.wasCastleBQ;
}

uint64_t getLegalMoves(Piece piece, int fromSq, Board* board, bool isWhite) {
    uint64_t pseudoMoves = getValidPieceMoves(piece, squareBit(fromSq), board, isWhite);
    uint64_t legalMoves = 0;

    while (pseudoMoves) {
        uint64_t lsb = pseudoMoves & (0ULL - pseudoMoves);
        int toSq = bitSquare(lsb);

        Move move = buildMove(board, piece, fromSq, toSq);
        makeMove(board, move);

        int kSq = isWhite ? bitSquare(board->bitboards[K])
            : bitSquare(board->bitboards[k]);
        if (!isSquareAttacked(kSq, !isWhite, board))
            legalMoves |= squareBit(toSq);

        unmakeMove(board, move);
        pseudoMoves &= pseudoMoves - 1;
    }
    return legalMoves;
}

//------------------piece interaction

void pickUpPiece(Board* board, int sq) {
    int start = board->sideToMove ? 0 : 6;
    int end = board->sideToMove ? 6 : 12;

    for (int i = start; i < end; i++) {
        if ((board->bitboards[i] >> sq) & 1) {
            board->heldPieceType = static_cast<Piece>(i);
            board->heldSquare = sq;
            return;
        }
    }
}

void dropPiece(Board* board, int to) {
    if (board->heldSquare == -1) return;

    bool isWhite = board->sideToMove;

    // Validate against legal moves only
    if (!(getLegalMoves(board->heldPieceType, board->heldSquare, board, isWhite) & squareBit(to))) {
        board->heldPieceType = None;
        board->heldSquare = -1;
        return;
    }

    Move move = buildMove(board, board->heldPieceType, board->heldSquare, to);
    makeMove(board, move);

    board->heldPieceType = None;
    board->heldSquare = -1;
}

//------------------visual functions

void drawOutline(int file, int rank, Color clr = BLACK) {
    int size = squareEdgeSide;
    Rectangle rec = {
        (float)(size * file + margin),
        (float)(size * rank + margin),
        (float)size, (float)size
    };
    DrawRectangleLinesEx(rec, 2, clr);
}

void drawPiece(Piece piece, Vector2 pos, Color tint = WHITE) {
    DrawTexture(textures[piece], (int)(pos.x + margin), (int)(pos.y + margin), tint);
}

void drawSquare(int file, int rank) {
    int size = squareEdgeSide;
    Color color = (rank + file) % 2 == 1 ? boardDarkColor : boardLightColor;
    DrawRectangle(size * file + margin, size * rank + margin, size, size, color);
}

void drawMoveIndicator(int file, int rank, bool isCapture) {
    int size = squareEdgeSide;
    int cx = (size * file + margin) + size / 2;
    int cy = (size * rank + margin) + size / 2;
    if (isCapture) drawOutline(file, rank, GREEN);
    else           DrawCircle(cx, cy, size / 6, availableMoveColor);
}

void drawPieceAt(Board* board, int sq, int file, int rank) {
    int size = squareEdgeSide;
    for (int i = 0; i < 12; i++) {
        if ((board->bitboards[i] >> sq) & 1) {
            drawPiece(static_cast<Piece>(i), { (float)(size * file), (float)(size * rank) });
            break;
        }
    }
}

void drawBoard(Board* board) {
    int size = squareEdgeSide;
    DrawRectangle(0, 0, size * 8 + margin * 2, size * 8 + margin * 2, backGroundColor);

    // Use getLegalMoves so indicators only show truly legal squares
    uint64_t availableMoves = 0;
    if (board->heldSquare != -1)
        availableMoves = getLegalMoves(
            board->heldPieceType,
            board->heldSquare,
            board,
            board->sideToMove
        );

    for (int rank = 7; rank >= 0; rank--) {
        for (int file = 0; file < 8; file++) {
            drawSquare(file, rank);

            if (Vector2Equals(board->highlightedSquare, { (float)file, (float)rank }))
                drawOutline(file, rank);

            int sq = (7 - rank) * 8 + file;
            bool isAvailable = (availableMoves >> sq) & 1;
            bool isOccupied = (board->occupied() >> sq) & 1;

            if (isAvailable) drawMoveIndicator(file, rank, isOccupied);
            if (!isOccupied) continue;

            drawPieceAt(board, sq, file, rank);
        }
    }
}

//------------------main loop

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    Board* board = new Board();

    string defFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    string FEN = "";
    bool validFen = false;

    while (!validFen) {
        char enterCustomFen = ' ';
        cout << "Want to enter custom FEN? Y/n : ";
        cin >> enterCustomFen;
        cout << "\n";

        if (enterCustomFen == 'Y') {
            cout << "Enter FEN: ";
            cin.ignore();
            getline(cin, FEN);
            cout << "\n";
            if (validateFEN(FEN)) validFen = true;
            else cout << "Invalid FEN format.\n";
        }
        else {
            cout << "Default game\n";
            FEN = defFEN;
            validFen = true;
        }
    }

    parseFEN(FEN, *board);

    const int size = (squareEdgeSide * 8) + (margin * 2);
    const int screenWidth = size + 16;
    const int screenHeight = size;

    InitWindow(screenWidth, screenHeight, "Chess");

    textures = {
        { P, LoadTexture("assets/w_pawn.png")   },
        { K, LoadTexture("assets/w_king.png")   },
        { Q, LoadTexture("assets/w_queen.png")  },
        { B, LoadTexture("assets/w_bishop.png") },
        { N, LoadTexture("assets/w_knight.png") },
        { R, LoadTexture("assets/w_rook.png")   },
        { p, LoadTexture("assets/b_pawn.png")   },
        { k, LoadTexture("assets/b_king.png")   },
        { q, LoadTexture("assets/b_queen.png")  },
        { b, LoadTexture("assets/b_bishop.png") },
        { n, LoadTexture("assets/b_knight.png") },
        { r, LoadTexture("assets/b_rook.png")   },
    };

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        Vector2 mousePos = GetMousePosition();
        board->highlightedSquare = {
            floorf((mousePos.x - margin) / squareEdgeSide),
            floorf((mousePos.y - margin) / squareEdgeSide)
        };

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            int file = (int)board->highlightedSquare.x;
            int rank = (int)board->highlightedSquare.y;
            int sq = square(file, 7 - rank);

            if (board->heldSquare == -1) pickUpPiece(board, sq);
            else                         dropPiece(board, sq);
        }

        BeginDrawing();
        drawBoard(board);
        EndDrawing();
    }

    for (auto& pair : textures) UnloadTexture(pair.second);
    CloseWindow();
    delete board;
    return 0;
}
