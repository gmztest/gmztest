#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

#include "print.h"

using std::cerr;
using std::endl;
using std::string;

/**
 *  Display the board in stderr.
 */
void PrintBoard(Board& b, int v) {

    // Output captions of x coordinate.
    string str_x = "ABCDEFGHJKLMNOPQRST";
    cerr << "  ";
    for (int x = 0; x < BSIZE; ++x)  cerr << " " << str_x[x] << " ";
    cerr << endl;

    for (int y = 0; y < BSIZE; ++y) {

        // Add indent when single digit.
        if (BSIZE - y < 10) cerr << " ";
        // Output y coordinate in reverse order because the lower left is (A,1).
        cerr << BSIZE - y;

        for (int x = 0; x < BSIZE; ++x) {
            int ev = rtoe[xytor[x][BSIZE - 1 - y]];
            bool is_star = false;
            if ((x == 3 || x == 9 || x == 15) &&
                (y == 3 || y == 9 || y == 15)) 
            {
                is_star = true;
            }

            if (v == ev) {
                // When previous move is white.
                if (b.my == 1) cerr << "[O]";
                // When previous move is black.
                else cerr << "[X]";
            }
            else if (b.color[ev] == 2) cerr << " O ";
            else if (b.color[ev] == 3) cerr << " X ";
            else if (is_star) cerr << " + "; // Star
            else cerr << " . ";
        }

        if (BSIZE - y < 10) cerr << " ";
        cerr << BSIZE - y << endl;

    }

    cerr << "  ";
    for (int x = 0; x < BSIZE; ++x)  cerr << " " << str_x[x] << " ";
    cerr << endl;

}

/**
 *  Output the board to the file.
 */
void PrintBoard(Board& b, int v, std::ofstream* log_file) {

    std::ofstream& ofs = *log_file;
    if (ofs.fail()) return;

    // Output captions of x coordinate.
    string str_x = "ABCDEFGHJKLMNOPQRST";
    ofs << "  ";
    for (int x = 0; x < BSIZE; ++x)  ofs << " " << str_x[x] << " ";
    ofs << endl;

    for (int y = 0; y < BSIZE; ++y) {

        // Add indent when single digit.
        if (BSIZE - y < 10) ofs << " ";
        // Output y coordinate in reverse order because the lower left is (A,1).
        ofs << BSIZE - y;

        for (int x = 0; x < BSIZE; ++x) {
            int ev = rtoe[xytor[x][BSIZE - 1 - y]];
            bool is_star = false;
            if ((x == 3 || x == 9 || x == 15) &&
                (y == 3 || y == 9 || y == 15)) 
            {
                is_star = true;
            }

            if (v == ev) {
                // When previous move is white.
                if (b.my == 1) ofs << "[O]";
                // When previous move is black.
                else ofs << "[X]";
            }
            else if (b.color[ev] == 2) ofs << " O ";
            else if (b.color[ev] == 3) ofs << " X ";
            else if (is_star) ofs << " + "; // Star
            else ofs << " . ";
        }

        if (BSIZE - y < 10) ofs << " ";
        ofs << BSIZE - y << endl;

    }

    ofs << "  ";
    for (int x = 0; x < BSIZE; ++x)  ofs << " " << str_x[x] << " ";
    ofs << endl;

}

void PrintEF(std::string str, std::ofstream& ofs) {
    std::cerr << str;
    if (ofs) ofs << str;
}

/**
 *  Output the final score.
 */
void PrintFinalScore(Board& b, int (&game_cnt)[3], int (&owner_cnt)[2][EBVCNT],
                     int win_pl, double komi, std::ofstream* log_file)
{

    std::ofstream& ofs = *log_file;

    // 1. Display dead stones.
    PrintEF("\ndead stones ([ ] = dead)\n", ofs);

    // Output captions of x coordinate.
    string str_x = "ABCDEFGHJKLMNOPQRST";
    PrintEF("  ", ofs);
    for (int x = 0; x < BSIZE; ++x) {
        string str_ = " "; str_ += str_x[x]; str_ += " ";
        PrintEF(str_, ofs);
    }
    PrintEF("\n", ofs);

    for (int y = 0; y < BSIZE; ++y) {

        // Add indent when single digit.
        if (BSIZE - y < 10) PrintEF(" " + std::to_string(BSIZE - y), ofs);
        else PrintEF(std::to_string(BSIZE - y), ofs);

        for (int x = 0; x < BSIZE; ++x) {
            int v = rtoe[xytor[x][BSIZE - 1 - y]];
            bool is_star = false;
            if ((x == 3 || x == 9 || x == 15) &&
                (y == 3 || y == 9 || y == 15)) 
            {
                is_star = true;
            }

            // Dead stone if occupancy is less than 50%.
            if (b.color[v] == 2 && (double)owner_cnt[0][v]/game_cnt[2] < 0.5) {
                PrintEF("[O]", ofs);
            } else if (b.color[v] == 3 && (double)owner_cnt[1][v]/game_cnt[2] < 0.5) {
                PrintEF("[X]", ofs);
            } else if (b.color[v] == 2) {
                PrintEF(" O ", ofs);
            } else if (b.color[v] == 3) {
                PrintEF(" X ", ofs);
            } else if (is_star) {
                PrintEF(" + ", ofs);
            } else {
                PrintEF(" . ", ofs);
            }
        }

        if (BSIZE - y < 10) PrintEF(" " + std::to_string(BSIZE - y) + "\n", ofs);
        else PrintEF(std::to_string(BSIZE - y) + "\n", ofs);

    }

    PrintEF("  ", ofs);
    for (int x = 0; x < BSIZE; ++x) {
        string str_ = " "; str_ += str_x[x]; str_ += " ";
        PrintEF(str_, ofs);
    }
    PrintEF("\n", ofs);

    // 2. Count taken stones for Japanese rule.
    int score_jp[2] = {0, 0};
    int score_cn[2] = {0, 0};
    int agehama[2];
    for (int i = 0; i < 2; ++i) {
        agehama[i] = b.move_cnt / 2 - b.stone_cnt[i];
        agehama[i] += int(i == 1)*int(b.move_cnt % 2 == 1) - b.pass_cnt[i];
    }

    // 3. Display occupied areas.
    PrintEF("\narea ([ ] = black area, ? ? = unknown)\n", ofs);

    // Output captions of x coordinate.
    PrintEF("  ", ofs);
    for (int x = 0; x < BSIZE; ++x) {
        string str_ = " "; str_ += str_x[x]; str_ += " ";
        PrintEF(str_, ofs);
    }
    PrintEF("\n", ofs);

    for (int y = 0; y < BSIZE; ++y) {

        // Add indent when single digit.
        if (BSIZE - y < 10) PrintEF(" " + std::to_string(BSIZE - y), ofs);
        else PrintEF(std::to_string(BSIZE - y), ofs);

        for (int x = 0; x < BSIZE; ++x) {
            int v = rtoe[xytor[x][BSIZE - 1 - y]];
            bool is_star = false;
            if ((x == 3 || x == 9 || x == 15) &&
                (y == 3 || y == 9 || y == 15)) 
            {
                is_star = true;
            }

            // When white occupancy is more than 50%.
            if ((double)owner_cnt[0][v]/game_cnt[2] > 0.5) {
                if (b.color[v] == 2) PrintEF(" O ", ofs);
                else if (b.color[v] == 3) {
                    PrintEF(" X ", ofs);
                    // For Japanese rule.
                    ++agehama[1];
                    ++score_jp[0];
                } else {
                    if (is_star) PrintEF(" + ", ofs);
                    else PrintEF(" . ", ofs);
                    ++score_jp[0];
                }

                ++score_cn[0];
            }
            // When black occupancy is more than 50%.
            else if ((double)owner_cnt[1][v]/game_cnt[2] > 0.5) {
                if (b.color[v] == 2) {
                    PrintEF("[O]", ofs);
                    // For Japanese rule.
                    ++agehama[0];
                    ++score_jp[1];
                } else if (b.color[v] == 3) {
                    PrintEF("[X]", ofs);
                } else {
                    if (is_star) PrintEF("[+]", ofs);
                    else PrintEF("[.]", ofs);
                    ++score_jp[1];
                }

                ++score_cn[1];
            }
            // Unknown area.
            else if (b.color[v] == 2) {
                PrintEF("?O?", ofs);
            }
            else if (b.color[v] == 3) {
                PrintEF("?X?", ofs);
            }
            else {
                if (is_star) PrintEF("?+?", ofs);
                else PrintEF("?.?", ofs);
            }
        }

        if (BSIZE - y < 10) PrintEF(" " + std::to_string(BSIZE - y) + "\n", ofs);
        else PrintEF(std::to_string(BSIZE - y) + "\n", ofs);

    }

    PrintEF("  ", ofs);
    for (int x = 0; x < BSIZE; ++x) {
        string str_ = " "; str_ += str_x[x]; str_ += " ";
        PrintEF(str_, ofs);
    }
    PrintEF("\n", ofs);

    // 4. Show final results.
    string str_win_pl = win_pl==0 ? "W+":"B+";

    double abs_score_jp = std::abs((score_jp[1]-agehama[1])-(score_jp[0]-agehama[0])-komi);
    double abs_score_cn = std::abs(score_cn[1] - score_cn[0] - komi);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "result: " << str_win_pl;
    ss << abs_score_cn << " (Chinese rule)" << endl;

    PrintEF(ss.str(), ofs);

    ss.str("");
    ss << "result: " << str_win_pl;
    ss << abs_score_jp << " (Japanese rule)" << endl;

    PrintEF(ss.str(), ofs);

}


/**
 *  Display occupancy of the vertexes in stderr.
 */
void PrintOccupancy(int (&game_cnt)[3], int (&owner_cnt)[2][EBVCNT]) {

    // Output captions of x coordinate.
    string str_x = "ABCDEFGHJKLMNOPQRST";
    cerr << "  ";
    for (int x = 0; x < BSIZE; ++x)  cerr << " " << str_x[x] << " ";
    cerr << endl;

    for (int y = 0; y < BSIZE; ++y) {

        // Add indent when single digit.
        if (BSIZE - y < 10) cerr << " ";
        // Output y coordinate in reverse order because the lower left is (A,1).
        cerr << BSIZE - y;

        for (int x = 0; x < BSIZE; ++x) {
            int v = rtoe[xytor[x][BSIZE - 1 - y]];

            // Occupancy of black stones.
            double black_ratio = (double)owner_cnt[1][v] / game_cnt[2];
            // Round up to integer [0,9]
            int disp_ratio = (int)round(black_ratio * 9);


            if (black_ratio >= 0.5) {
                // When black occupancy is more than 50%.
                cerr << "[" << disp_ratio << "]";
            } else {
                // When white occupancy is more than 50%.
                cerr << " " << disp_ratio << " ";
            }
        }

        if (BSIZE - y < 10) cerr << " ";
        cerr << BSIZE - y << endl;

    }

    cerr << "  ";
    for (int x = 0; x < BSIZE; ++x)  cerr << " " << str_x[x] << " ";
    cerr << endl;

}


/**
 *  Write occupancy of the vertexes in file.
 */
void PrintOccupancy(int (&game_cnt)[3], int (&owner_cnt)[2][EBVCNT], std::ofstream* log_file) {

    std::ofstream& ofs = *log_file;
    // Output captions of x coordinate.
    string str_x = "ABCDEFGHJKLMNOPQRST";
    ofs << "  ";
    for (int x = 0; x < BSIZE; ++x)  ofs << " " << str_x[x] << " ";
    ofs << endl;

    for (int y = 0; y < BSIZE; ++y) {

        // Add indent when single digit.
        if (BSIZE - y < 10) ofs << " ";
        // Output y coordinate in reverse order because the lower left is (A,1).
        ofs << BSIZE - y;

        for (int x = 0; x < BSIZE; ++x) {
            int v = rtoe[xytor[x][BSIZE - 1 - y]];

            // Occupancy of black stones.
            double black_ratio = (double)owner_cnt[1][v] / game_cnt[2];
            // Round up to integer [0,9]
            int disp_ratio = (int)round(black_ratio * 9);


            if (black_ratio >= 0.5) {
                // When black occupancy is more than 50%.
                ofs << "[" << disp_ratio << "]";
            } else {
                // When white occupancy is more than 50%.
                ofs << " " << disp_ratio << " ";
            }
        }

        if (BSIZE - y < 10) ofs << " ";
        ofs << BSIZE - y << endl;

    }

    ofs << "  ";
    for (int x = 0; x < BSIZE; ++x)  ofs << " " << str_x[x] << " ";
    ofs << endl;

}


/**
 *  Display the probability distribution in stderr.
 */
void PrintProb(Board& b, std::array < float,BVCNT>& prob) {

    // Output captions of x coordinate.
    string str_x = "ABCDEFGHJKLMNOPQRST";
    cerr << "  ";
    for (int x = 0; x < BSIZE; ++x)  cerr << " " << str_x[x] << " ";
    cerr << endl;

    float inv_prob = 0.0;
    for (auto& p: prob) inv_prob += p;
    if (inv_prob > 0) inv_prob = 1 / inv_prob;
    else inv_prob = 1.0;

    for (int y = 0; y < BSIZE; ++y) {

        // Add indent when single digit.
        if (BSIZE - y < 10) cerr << " ";
        // Output y coordinate in reverse order because the lower left is (A,1).
        cerr << BSIZE - y;

        for (int x = 0; x < BSIZE; ++x) {
            int rv = xytor[x][BSIZE - 1 - y];
            int ev = rtoe[xytor[x][BSIZE - 1 - y]];
            bool is_star = false;
            if ((x == 3 || x == 9 || x == 15) &&
                (y == 3 || y == 9 || y == 15)) 
            {
                is_star = true;
            }

            if (prob[rv] > 0) {
                int disp_prob = int((prob[rv] * inv_prob) * 99);
                if (disp_prob > 9) cerr << " " << disp_prob;
                else cerr << "  " << disp_prob;
            }
            else if (b.color[ev] == 2) cerr << " O ";
            else if (b.color[ev] == 3) cerr << " X ";
            else if (is_star) cerr << " + "; // Star
            else cerr << " . ";
        }

        if (BSIZE - y < 10) cerr << " ";
        cerr << BSIZE - y << endl;

    }

    cerr << "  ";
    for (int x = 0; x < BSIZE; ++x)  cerr << " " << str_x[x] << " ";
    cerr << endl;

}
