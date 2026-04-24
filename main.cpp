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

int square(int file, int rank) { return rank * 8 + file; }

bool validateFEN(const string& fen) {
    // Split FEN into fields
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

    // --- Field 1: Piece placement ---
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

    if (whiteKings != 1) {
        cout << "FEN Error: Expected 1 white king, found " << whiteKings << "\n";
        return false;
    }
    if (blackKings != 1) {
        cout << "FEN Error: Expected 1 black king, found " << blackKings << "\n";
        return false;
    }

    // --- Field 2: Side to move ---
    if (fields[1] != "w" && fields[1] != "b") {
        cout << "FEN Error: Side to move must be 'w' or 'b', got '" << fields[1] << "'\n";
        return false;
    }

    // --- Field 3: Castling rights ---
    if (fields[2] != "-") {
        for (char c : fields[2]) {
            if (string("KQkq").find(c) == string::npos) {
                cout << "FEN Error: Invalid castling character '" << c << "'\n";
                return false;
            }
        }
    }

    // --- Field 4: En passant ---
    if (fields[3] != "-") {
        if (fields[3].size() != 2
            || fields[3][0] < 'a' || fields[3][0] > 'h'
            || (fields[3][1] != '3' && fields[3][1] != '6')) {
            cout << "FEN Error: Invalid en passant square '" << fields[3] << "'\n";
            return false;
        }
    }

    // --- Field 5: Halfmove clock ---
    for (char c : fields[4]) {
        if (!isdigit(c)) {
            cout << "FEN Error: Halfmove clock must be a number, got '" << fields[4] << "'\n";
            return false;
        }
    }

    // --- Field 6: Fullmove number ---
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

enum Piece : uint8_t
{
    R,N,B,Q,K,P,r,n,b,q,k,p,None
};
class Board {
public:

    uint64_t bitboards[12] = { 0 }; // 0-5 White (P,N,B,R,Q,K), 6-11 Black (p,n,b,r,q,k)
    bool sideToMove=true;//true = white,false = black

    bool castleWK = false; // White kingside
    bool castleWQ = false; // White queenside
    bool castleBK = false; // Black kingside
    bool castleBQ = false; // Black queenside

    uint64_t whitePieces() 
    { 
        return bitboards[P] | bitboards[N] | bitboards[B] | bitboards[R] | bitboards[Q] | bitboards[K] ;

    }

    uint64_t blackPieces() {
        return bitboards[p] | bitboards[n] | bitboards[b] | bitboards[r] | bitboards[q] | bitboards[k];
    }

    uint64_t occupied()    { return whitePieces() | blackPieces(); }

    Vector2 highlightedSquare = { 0,0 };

    uint64_t* heldPiece = nullptr;
    Piece heldPieceType = None;
    int heldSquare = -1;
    
};

map<Piece, Texture2D> textures = {};
int squareEdgeSide = 64;
int margin = 32;

Color backGroundColor = { 79,32,13,255 };
Color boardDarkColor = BROWN;
Color boardLightColor = LIGHTGRAY;
Color availableMoveColor = { 10, 200, 50, 150 };

const uint64_t RANK_3 = 0x0000000000FF0000ULL; // White pawns start here
const uint64_t RANK_6 = 0x0000FF0000000000ULL; // Black pawns start here

//------------------logic functions

void parseFEN(const std::string& fen, Board& board) {
    int rank = 7; // FEN starts at rank 8 (index 7)
    int file = 0; // FEN starts at file A (index 0)
    char lastChar = '.';
    for (char c : fen) {
        if (c == ' ') {
            lastChar = c;
            break; // End of piece placement section
        }

        if (lastChar == ' ') {
            switch (c) {
                case 'w': board.sideToMove = true; break;
                case 'b': board.sideToMove = false; break;
            }
        }
        lastChar = c;

        if (c == '/') {
            rank--;
            file = 0;
        }
        else if (isdigit(c)) {
            file += (c - '0'); // Skip empty squares
        }
        else {
            // Map character to Piece enum
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
                int square = rank * 8 + file;
                board.bitboards[piece] |= (1ULL << square);
                file++;
            }
        }
    }
    //------castling
    int spaces = 0;
    for (int i = 0; i < fen.size(); i++) {
        if (fen[i] == ' ') spaces++;
        if (spaces == 2) {
            for (int j = i + 1; j < fen.size() && fen[j] != ' '; j++) {
                if (fen[j] == 'K') board.castleWK = true;
                if (fen[j] == 'Q') board.castleWQ = true;
                if (fen[j] == 'k') board.castleBK = true;
                if (fen[j] == 'q') board.castleBQ = true;
            }
            break;
        }
    }

}

//get moves functions

uint64_t getRookAttacks(uint64_t rook, uint64_t occupied, uint64_t friendly) {
    uint64_t result = 0;
    uint64_t ray;

    // North (+8 per rank)
    ray = rook;
    while ((ray = (ray << 8))) {
        if (ray & friendly) break;          // blocked by own piece, stop before
        result |= ray;
        if (ray & occupied) break;          // hit enemy piece, include but stop
    }

    // South (-8 per rank)
    ray = rook;
    while ((ray = (ray >> 8))) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }

    // East (+1 per file)
    ray = rook;
    while ((ray = (ray << 1) & NOT_A_FILE)) {  // NOT_A_FILE prevents h->a wrap
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }

    // West (-1 per file)
    ray = rook;
    while ((ray = (ray >> 1) & NOT_H_FILE)) {  // NOT_H_FILE prevents a->h wrap
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }

    return result;
}

uint64_t getValidRookMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t occupied = board->whitePieces() | board->blackPieces();
    uint64_t result = 0;

    while (pieceBitboard) {
        uint64_t singleRook = pieceBitboard & (0ULL - pieceBitboard);
        result |= getRookAttacks(singleRook, occupied, friendly);
        pieceBitboard &= pieceBitboard - 1;                   // clear LSB
    }

    return result;
}

uint64_t getValidKnightMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t result = 0;

    uint64_t notAFile = NOT_A_FILE;  // 0xFEFEFEFEFEFEFEFE
    uint64_t notHFile = NOT_H_FILE;  // 0x7F7F7F7F7F7F7F7F
    uint64_t notABFile = notAFile & (notAFile << 1); // masks A and B
    uint64_t notGHFile = notHFile & (notHFile >> 1); // masks G and H

    result |= (pieceBitboard << 17) & notAFile;  // +2 rank, +1 file
    result |= (pieceBitboard << 15) & notHFile;  // +2 rank, -1 file
    result |= (pieceBitboard << 10) & notABFile; // +1 rank, +2 file
    result |= (pieceBitboard << 6) & notGHFile; // +1 rank, -2 file
    result |= (pieceBitboard >> 17) & notHFile;  // -2 rank, -1 file
    result |= (pieceBitboard >> 15) & notAFile;  // -2 rank, +1 file
    result |= (pieceBitboard >> 10) & notGHFile; // -1 rank, -2 file
    result |= (pieceBitboard >> 6) & notABFile; // -1 rank, +2 file

    return result & ~friendly; // exclude squares occupied by own pieces
}

uint64_t getBishopAttacks(uint64_t bishop, uint64_t occupied, uint64_t friendly) {
    uint64_t result = 0;
    uint64_t ray;

    // North-East
    ray = bishop;
    while ((ray = (ray << 9) & NOT_A_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }

    // North-West
    ray = bishop;
    while ((ray = (ray << 7) & NOT_H_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }

    // South-East
    ray = bishop;
    while ((ray = (ray >> 7) & NOT_A_FILE)) {
        if (ray & friendly) break;
        result |= ray;
        if (ray & occupied) break;
    }

    // South-West
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
        uint64_t singleBishop = pieceBitboard & (0ULL - pieceBitboard); // isolate LSB
        result |= getBishopAttacks(singleBishop, occupied, friendly);
        pieceBitboard &= pieceBitboard - 1; // clear LSB
    }

    return result;
}

uint64_t getValidQueenMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    // Queen = rook + bishop
    return getValidRookMoves(pieceBitboard, board, isWhite)
        | getValidBishopMoves(pieceBitboard, board, isWhite);
}

// TODO : EN-PASSUNT
uint64_t getValidPawnMoves(uint64_t pieceBitboard, Board* board, bool isWhite) {
    uint64_t friendly = isWhite ? board->whitePieces() : board->blackPieces();
    uint64_t against = (!isWhite) ? board->whitePieces() : board->blackPieces();
    uint64_t result = 0;

    if (isWhite) {
        // Single and double push
        uint64_t empty = ~(friendly | against);
        uint64_t singlePush = (pieceBitboard << 8) & empty;
        uint64_t doublePush = ((singlePush & RANK_3) << 8) & empty; // RANK_3 = where white pawns are after one push from rank 2

        result |= singlePush | doublePush;

        result |= (pieceBitboard << 9) & NOT_A_FILE & against; // North-East
        result |= (pieceBitboard << 7) & NOT_H_FILE & against; // North-West
    }
    else {
        uint64_t empty = ~(friendly | against);
        uint64_t singlePush = (pieceBitboard >> 8) & empty;
        uint64_t doublePush = ((singlePush & RANK_6) >> 8)& empty; // RANK_3 = where white pawns are after one push from rank 2

        result |= singlePush | doublePush;

        result |= (pieceBitboard >> 7) & NOT_A_FILE & against; // South-East
        result |= (pieceBitboard >> 9) & NOT_H_FILE & against; // South-West
    }
    
    return result; // Cannot move onto your own pieces
} 

bool isSquareAttacked(int sq, bool byWhite, Board* board) {
    uint64_t sqBit = squareBit(sq);

    // Check knight attacks
    uint64_t knights = byWhite ? board->bitboards[N] : board->bitboards[n];
    if (getValidKnightMoves(sqBit, board, !byWhite) & knights) return true;

    // Check bishop/queen diagonal attacks
    uint64_t bishops = byWhite ? (board->bitboards[B] | board->bitboards[Q])
        : (board->bitboards[b] | board->bitboards[q]);
    if (getValidBishopMoves(sqBit, board, !byWhite) & bishops) return true;

    // Check rook/queen straight attacks
    uint64_t rooks = byWhite ? (board->bitboards[R] | board->bitboards[Q])
        : (board->bitboards[r] | board->bitboards[q]);
    if (getValidRookMoves(sqBit, board, !byWhite) & rooks) return true;

    // Check pawn attacks
    uint64_t pawns = byWhite ? board->bitboards[P] : board->bitboards[p];
    if (byWhite) {
        // White pawns attack diagonally upward, so check if sq is attacked from below
        if (((sqBit >> 9) & NOT_H_FILE & pawns) || ((sqBit >> 7) & NOT_A_FILE & pawns)) return true;
    }
    else {
        if (((sqBit << 9) & NOT_A_FILE & pawns) || ((sqBit << 7) & NOT_H_FILE & pawns)) return true;
    }

    // King - raw pattern instead of calling getValidKingMoves (avoids circular call)
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

    // Up/Down (No file-wrapping possible)
    result |= (pieceBitboard << 8); // North
    result |= (pieceBitboard >> 8); // South

    // East moves (Protect against A-file wrap)
    result |= (pieceBitboard << 1) & NOT_A_FILE; // East
    result |= (pieceBitboard << 9) & NOT_A_FILE; // North-East
    result |= (pieceBitboard >> 7) & NOT_A_FILE; // South-East

    // West moves (Protect against H-file wrap)
    result |= (pieceBitboard >> 1) & NOT_H_FILE; // West
    result |= (pieceBitboard << 7) & NOT_H_FILE; // North-West
    result |= (pieceBitboard >> 9) & NOT_H_FILE; // South-West

    if (isWhite) {
        // King must be on e1 and not in check
        if (pieceBitboard & squareBit(4) && !isSquareAttacked(4, !isWhite, board)) {
            // Kingside: f1(5) and g1(6) empty and not attacked, rook on h1(7)
            if (board->castleWK
                && !(occ & squareBit(5)) && !(occ & squareBit(6))
                && !isSquareAttacked(5, !isWhite, board)
                && !isSquareAttacked(6, !isWhite, board))
                result |= squareBit(6);

            // Queenside: d1(3), c1(2), b1(1) empty, d1+c1 not attacked, rook on a1(0)
            if (board->castleWQ
                && !(occ & squareBit(3)) && !(occ & squareBit(2)) && !(occ & squareBit(1))
                && !isSquareAttacked(3, !isWhite, board)
                && !isSquareAttacked(2, !isWhite, board))
                result |= squareBit(2);
        }
    }
    else {
        // King must be on e8(60) and not in check
        if (pieceBitboard & squareBit(60) && !isSquareAttacked(60, !isWhite, board)) {
            // Kingside: f8(61) and g8(62) empty and not attacked, rook on h8(63)
            if (board->castleBK
                && !(occ & squareBit(61)) && !(occ & squareBit(62))
                && !isSquareAttacked(61, !isWhite, board)
                && !isSquareAttacked(62, !isWhite, board))
                result |= squareBit(62);

            // Queenside: d8(59), c8(58), b8(57) empty, d8+c8 not attacked, rook on a8(56)
            if (board->castleBQ
                && !(occ & squareBit(59)) && !(occ & squareBit(58)) && !(occ & squareBit(57))
                && !isSquareAttacked(59, !isWhite, board)
                && !isSquareAttacked(58, !isWhite, board))
                result |= squareBit(58);
        }
    }

    return result & ~friendly; // Cannot move onto your own pieces
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

//piece handling functions

void movePiece(uint64_t& pieceBitboard, int from, int to) {
    pieceBitboard &= ~squareBit(from); // clear source
    pieceBitboard |= squareBit(to);   // set destination
}

void pickUpPiece(Board* board, int sq) {

    if (board->sideToMove) {

        for (size_t i = 0; i < 6; i++)
        {
            if ((board->bitboards[i] >> sq) & 1) {
                board->heldPiece = &board->bitboards[i];
                board->heldPieceType = static_cast<Piece>(i);
                break;
            }
        }

    }
    else  {

        for (size_t i = 6; i < 12; i++)
        {
            if ((board->bitboards[i] >> sq) & 1) {
                board->heldPiece = &board->bitboards[i];
                board->heldPieceType = static_cast<Piece>(i);
                break;
            }
        }

    }

    board->heldSquare = sq;
}

void dropPiece(Board* board, int to) {
    if (board->heldPiece == nullptr) return;

    bool movingWhite = (board->whitePieces() >> board->heldSquare) & 1;

    // FIX: Pass only the single bit of the held piece, not the whole bitboard
    uint64_t movingPieceBit = squareBit(board->heldSquare);

    if (!(getValidPieceMoves(board->heldPieceType, movingPieceBit, board, movingWhite) & squareBit(to))) {
        board->heldPiece = nullptr;
        board->heldSquare = -1;
        return;
    }

    // Capture logic
    if (movingWhite) {
        for (size_t i = 6; i < 12; i++)
            board->bitboards[i] &= ~squareBit(to);
    }
    else {
        for (size_t i = 0; i < 6; i++)
            board->bitboards[i] &= ~squareBit(to);
    }

    movePiece(*board->heldPiece, board->heldSquare, to);

    if (board->heldPieceType == K) {
        board->castleWK = board->castleWQ = false;
        if (to == 6) movePiece(board->bitboards[R], 7, 5); // kingside
        if (to == 2) movePiece(board->bitboards[R], 0, 3); // queenside
    }
    if (board->heldPieceType == k) {
        board->castleBK = board->castleBQ = false;
        if (to == 62) movePiece(board->bitboards[r], 63, 61);
        if (to == 58) movePiece(board->bitboards[r], 56, 59);
    }
    // Revoke rook castling rights if rook moves
    if (board->heldPieceType == R) {
        if (board->heldSquare == 0) board->castleWQ = false;
        if (board->heldSquare == 7) board->castleWK = false;
    }
    if (board->heldPieceType == r) {
        if (board->heldSquare == 56) board->castleBQ = false;
        if (board->heldSquare == 63) board->castleBK = false;
    }

    board->heldPiece = nullptr;
    board->heldSquare = -1;
    board->sideToMove = !board->sideToMove;
}

//------------------visual functions

void drawOutline(int file, int rank,Color clr = BLACK) {
    int size = squareEdgeSide;
    Rectangle rec = {
        (float)(size * file + margin),
        (float)(size * rank + margin),
        (float)size,
        (float)size
    };
    DrawRectangleLinesEx(rec, 2, clr);
}

void drawPiece(Piece piece, Vector2 pos,Color tint=WHITE) {
    pos.x = pos.x + margin;
    pos.y = pos.y + margin;
    DrawTexture(textures[piece], pos.x,pos.y, tint);
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
    if (isCapture)
        drawOutline(file, rank, GREEN);
    else
        DrawCircle(cx, cy, size / 6, availableMoveColor);
}

void drawPieceAt(Board* board, int square, int file, int rank) {
    int size = squareEdgeSide;
    for (size_t i = 0; i < 12; i++) {
        if ((board->bitboards[i] >> square) & 1) {
            Vector2 pos = { (float)(size * file), (float)(size * rank) };
            drawPiece(static_cast<Piece>(i), pos);
            break;
        }
    }
}

void drawBoard(Board* board) {
    int size = squareEdgeSide;
    DrawRectangle(0, 0, size * 8 + margin * 2, size * 8 + margin * 2, backGroundColor);

    uint64_t availableMoves = 0;
    if (board->heldPiece != nullptr)
        availableMoves = getValidPieceMoves(
            board->heldPieceType,
            squareBit(board->heldSquare),
            board,
            board->sideToMove
        );

    for (int rank = 7; rank >= 0; rank--) {
        for (int file = 0; file < 8; file++) {
            drawSquare(file, rank);

            // Highlight hovered square
            if (Vector2Equals(board->highlightedSquare, { (float)file, (float)rank }))
                drawOutline(file, rank);

            int sq = (7 - rank) * 8 + file;
            bool isAvailable = (availableMoves >> sq) & 1;
            bool isOccupied = (board->occupied() >> sq) & 1;

            if (isAvailable)
                drawMoveIndicator(file, rank, isOccupied); // circle or green outline

            if (!isOccupied) continue; // <-- was return, which exited drawBoard early

            drawPieceAt(board, sq, file, rank);
        }
    }
}

//------------------main loop

int main(void)
{
    SetTraceLogLevel(LOG_WARNING); // only shows warnings and errors
    // Initialization
    //--------------------------------------------------------------------------------------
    Board* board = new Board();

    string defFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    string FEN = "";
    bool validFen = false; 

    while (!validFen) {

        char enterCustomFen = ' ';//Y=yes,n=no
        cout << "Want to enter custom parseFEN ? Y/n : ";
        cin >> enterCustomFen;
        cout << "\n";

        if (enterCustomFen == 'Y') {
            cout << "enter custom parseFen (--/--/--/--/--/--/--/-- (w/b) KQkq - 0 1) : ";
            cin.ignore(); // flush the leftover newline from cin >> enterCustomFen
            getline(cin, FEN);
            cout << "\n";
            if (validateFEN(FEN)) validFen = true;
            else cout << "invalid format\n";
        }
        else {
            cout << "Default game";
            FEN = defFEN;
            validFen = true;
        }

    }
    
    parseFEN(FEN, *board);

    const int size = (squareEdgeSide * 8) + (margin * 2);
    const int screenWidth = size+16;
    const int screenHeight = size;

    InitWindow(screenWidth, screenHeight, "raylib [core] example - basic window");

    textures = {
        // White pieces
        { P,  LoadTexture("assets/w_pawn.png")},
        { K,  LoadTexture("assets/w_king.png")   },
        { Q,  LoadTexture("assets/w_queen.png")  },
        { B,  LoadTexture("assets/w_bishop.png") },
        { N,  LoadTexture("assets/w_knight.png") },
        { R,  LoadTexture("assets/w_rook.png")   },
        //black pieces
        { p, LoadTexture("assets/b_pawn.png")   },
        { k, LoadTexture("assets/b_king.png")   },
        { q, LoadTexture("assets/b_queen.png")  },
        { b, LoadTexture("assets/b_bishop.png") },
        { n, LoadTexture("assets/b_knight.png") },
        { r, LoadTexture("assets/b_rook.png")   },
    };

    SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------
        Vector2 mousePos = GetMousePosition();
        board->highlightedSquare = {
        floorf((mousePos.x - margin) / squareEdgeSide),
        floorf((mousePos.y - margin) / squareEdgeSide)
        };
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            int file = (int)board->highlightedSquare.x;
            int rank = (int)board->highlightedSquare.y;

            int sq = square(file, 7 - rank); // <-- flip rank here
            if (board->heldPiece == nullptr)
                pickUpPiece(board,  sq);
            else
                dropPiece(board, sq);
        }
        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

        drawBoard(board);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    for (auto& pair : textures) {
        UnloadTexture(pair.second);
    }
    CloseWindow();     // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}

// *792 -- lines*