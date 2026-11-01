#include "maze.h"

static const int8_t mazeDX[4] = {0, 1, 0, -1};
static const int8_t mazeDY[4] = {-1, 0, 1, 0};
static const uint8_t wallBit[4] = {WALL_N, WALL_E, WALL_S, WALL_W};

void Maze::removeWall(int x, int y, int dir){
  int nx = x + mazeDX[dir];
  int ny = y + mazeDY[dir];
  if( nx < 0 || ny < 0 || nx >= cols || ny >= rows ){
    return;   // never open the outer border
  }
  walls[y][x] &= ~wallBit[dir];
  walls[ny][nx] &= ~wallBit[(dir + 2) % 4];
}

void Maze::generate(int c, int r){
  cols = min(c, mazeMaxCols);
  rows = min(r, mazeMaxRows);
  bool visited[mazeMaxRows][mazeMaxCols] = {};
  for( int y = 0; y < rows; y++ ){
    for( int x = 0; x < cols; x++ ){
      walls[y][x] = WALL_N | WALL_E | WALL_S | WALL_W;
    }
  }

  uint8_t stack[mazeMaxRows * mazeMaxCols];
  int top = 0;
  stack[top++] = 0;
  visited[0][0] = true;
  while( top > 0 ){
    int cell = stack[top - 1];
    int x = cell % cols, y = cell / cols;
    int options[4], n = 0;
    for( int d = 0; d < 4; d++ ){
      int nx = x + mazeDX[d], ny = y + mazeDY[d];
      if( nx >= 0 && ny >= 0 && nx < cols && ny < rows && !visited[ny][nx] ){
        options[n++] = d;
      }
    }
    if( n == 0 ){
      top--;
      continue;
    }
    int d = options[random(n)];
    removeWall(x, y, d);
    int nx = x + mazeDX[d], ny = y + mazeDY[d];
    visited[ny][nx] = true;
    stack[top++] = ny * cols + nx;
  }
}

void Maze::braid(int percent){
  for( int y = 0; y < rows; y++ ){
    for( int x = 0; x < cols; x++ ){
      if( countWalls(x, y) == 3 && random(100) < percent ){
        int options[4], n = 0;
        for( int d = 0; d < 4; d++ ){
          int nx = x + mazeDX[d], ny = y + mazeDY[d];
          if( (walls[y][x] & wallBit[d]) && nx >= 0 && ny >= 0 && nx < cols && ny < rows ){
            options[n++] = d;
          }
        }
        if( n > 0 ){
          removeWall(x, y, options[random(n)]);
        }
      }
    }
  }
}

bool Maze::open(int x, int y, int dir) const {
  return !(walls[y][x] & wallBit[dir]);
}

int Maze::countWalls(int x, int y) const {
  int n = 0;
  for( int d = 0; d < 4; d++ ){
    if( walls[y][x] & wallBit[d] ) n++;
  }
  return n;
}
