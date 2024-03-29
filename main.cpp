#include <ViZDoom.h>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <algorithm>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/viz/types.hpp>
#include <highgui.h>

void sleep(unsigned int time) {
    std::this_thread::sleep_for(std::chrono::milliseconds(time));
}

using namespace vizdoom;

using namespace cv;

using namespace std;

int main() {
    Ptr<FeatureDetector> detector = ORB::create(10000);

    std::cout << "\n\nBASIC EXAMPLE\n\n";

    // Create DoomGame instance. It will run the game and communicate with you.
    auto *game = new DoomGame();
    game->loadConfig("health_gathering2.cfg");
    game->init();
    /* Цикл for, который создаст по одному action для каждой кнопки
     * for (int i = 0; i < game->getAvailableButtonsSize(); i++) {
        std::vector<double> action;
        for (int j = 0; j < game->getAvailableButtonsSize(); j++) {
            if (j == i)
                action.push_back(1);
            else
                action.push_back(0);
        }
        actions[i] = action;
    }*/

    vector<double> didaction = {0, 1, 0};


    std::srand(time(nullptr));

    // Run this many episodes
    int episodes = 20;
    // Sets time that will pause the engine after each action.
    // Without this everything would go too fast for you to keep track of what's happening.
    unsigned int sleepTime = 1000 / DEFAULT_TICRATE; // = 28
    namedWindow("diff", WINDOW_AUTOSIZE);

    Mat diff(game->getScreenHeight(), game->getScreenWidth(), CV_8UC3);
    Mat now(game->getScreenHeight(), game->getScreenWidth(), CV_8UC3);
    Mat1b prev(game->getScreenHeight(), game->getScreenWidth(), CV_8UC3);
    Mat3b lbl(game->getScreenHeight(), game->getScreenWidth(), Vec3b(0, 0, 0));
    const int d = 190;

    bool moved;
    int notMoved = 0;
    double itog = 0;
    for (int i = 0; i < 1; ++i) {

        std::cout << "Episode #" << i + 1 << "\n";

        // Starts a new episode. It is not needed right after init() but it doesn't cost much and the loop is nicer.
        game->newEpisode();

        while (!game->isEpisodeFinished()) {

            moved = false;

            // Get the state
            GameStatePtr state = game->getState(); // GameStatePtr is std::shared_ptr<GameState>

            // Which consists of:
            unsigned int n = state->number;
            std::vector<double> vars = state->gameVariables;
            BufferPtr screenBuf = state->screenBuffer;
            BufferPtr depthBuf = state->depthBuffer;
            BufferPtr labelsBuf = state->labelsBuffer;
            BufferPtr automapBuf = state->automapBuffer;
            // BufferPtr is std::shared_ptr<Buffer> where Buffer is std::vector<uint8_t>
            std::vector<Label> labels = state->labels;

            int rows = game->getScreenHeight(), cols = game->getScreenWidth();
            // Выделение наиболее ярких зон
            for (int k = 0; k < now.rows; ++k) {
                for (int j = 0; j < now.cols; ++j) {
                    auto vectorCoord = 3 * (k * now.cols + j);

                    now.at<uchar>(k, 3 * j + 0) = (*screenBuf)[vectorCoord + 2];
                    now.at<uchar>(k, 3 * j + 1) = (*screenBuf)[vectorCoord + 1];
                    now.at<uchar>(k, 3 * j + 2) = (*screenBuf)[vectorCoord + 0];
                }
            }

            for (int k = 0; k < now.rows; ++k) {
                for (int j = 0; j < now.cols; ++j) {
                    int b = now.at<uchar>(k, 3 * j + 2);
                    int g = now.at<uchar>(k, 3 * j + 1);
                    int r = now.at<uchar>(k, 3 * j + 0);

                    int b1 = prev.at<uchar>(k, 3 * j + 2);
                    int g1 = prev.at<uchar>(k, 3 * j + 1);
                    int r1 = prev.at<uchar>(k, 3 * j + 0);

                    int B = abs(b - b1);
                    int G = abs(g - g1);
                    int R = abs(r - r1);

                    double A = sqrt(0.299 * r * r + 0.587 * b * b + 0.114 * g * g);

                    if (A > d) {
                        diff.at<uchar>(k, 3 * j + 0) = 255;
                        diff.at<uchar>(k, 3 * j + 1) = 255;
                        diff.at<uchar>(k, 3 * j + 2) = 255;
                    } else {
                        diff.at<uchar>(k, 3 * j + 0) = 0;
                        diff.at<uchar>(k, 3 * j + 1) = 0;
                        diff.at<uchar>(k, 3 * j + 2) = 0;
                    }
                }
            }

            std::vector<Point> pts;
            std::vector<int> labls;
            cvtColor(diff, prev, COLOR_RGB2GRAY);
            findNonZero(prev, pts);
            double dst = 7, dst2 = dst * dst;
            int nLabels = 0;
            if (pts.size() > 1) {
                nLabels = cv::partition(pts, labls, [dst2](const Point &lhs, const Point &rhs) {
                    return (hypot(lhs.x - rhs.x, lhs.y - rhs.y) < dst2);
                });
            } else {
                game->makeAction(didaction);
                continue;
            }

            if (nLabels == 0)
                continue;
            std::vector<Point> middles(nLabels);
            std::vector<Point> goout;
            std::vector<int> count(nLabels, 0);
            std::vector<vector<int>> RLTB;

            for (int k = 0; k < nLabels; ++k) {
                vector<int> tmp = {game->getScreenWidth(), 0, game->getScreenHeight(), 0};
                RLTB.push_back(tmp);
            }

            for (int k = 0; k < pts.size(); ++k) {
                count[labls[k]]++;
                middles[labls[k]].x += pts[k].x;
                middles[labls[k]].y += pts[k].y;

                if (RLTB[labls[k]][0] > pts[k].x)
                    RLTB[labls[k]][0] = pts[k].x;
                if (RLTB[labls[k]][1] < pts[k].x)
                    RLTB[labls[k]][1] = pts[k].x;
                if (RLTB[labls[k]][2] > pts[k].y)
                    RLTB[labls[k]][2] = pts[k].y;
                if (RLTB[labls[k]][3] < pts[k].y)
                    RLTB[labls[k]][3] = pts[k].y;
            }
            for (int k = 0; k < nLabels; ++k) {

                middles[k].x /= count[k];
                middles[k].y /= count[k];

                int hgt = abs(RLTB[k][1] - RLTB[k][0]);
                int wdt = abs(RLTB[k][3] - RLTB[k][2]);

                if (middles[k].y > 0.79 * game->getScreenHeight() || wdt > hgt) {

                    if (wdt > hgt)
                        goout.push_back(middles[k]);

                    middles.erase(middles.begin() + k);
                    RLTB.erase(RLTB.begin() + k);
                    count.erase(count.begin() + k--);
                    nLabels--;
                }
            }

            if (nLabels <= 0) {
                game->makeAction(didaction);
            } else {

                int K = 0, max = 0;
                for (int j = 0; j < nLabels; ++j) {
                    if (count[j] > max) {
                        max = count[j];
                        K = j;
                    }
                }

                if (middles[K].x < game->getScreenWidth() / 2 - 10) {
                    game->makeAction({1, 0, 1});
                    didaction = {1, 0, 0};
                } else if (middles[K].x > game->getScreenWidth() / 2 + 10) {
                    game->makeAction({0, 1, 1});
                    didaction = {0, 1, 0};
                }

                bool cnt = false;
                for (auto &j : goout) {
                    if (j.x >= game->getScreenWidth() / 2 - 20 and
                        j.x <= game->getScreenWidth() / 2 + 20) {
                        game->makeAction(didaction);
                        cnt = true;
                        break;
                    }
                }

                if (cnt)
                    continue;

                game->makeAction({0, 0, 1});
            }

            // Make random action and get reward

            // You can also get last reward by using this function
            // double reward = game->getLastReward();

            // Makes a "prolonged" action and skip frames.
            //int skiprate = 4
            //double reward = game.makeAction(choice(actions), skiprate)

            // The same could be achieved with:
            //game.setAction(choice(actions))
            //game.advanceAction(skiprate)
            //reward = game.getLastReward()

            //std::cout << "State #" << n << "\n";
            //std::cout << "Game variables: " << vars[0] << "\n";
            //std::cout << "Action reward: " << reward << "\n";
        }


        std::cout << "Episode finished.\n";
        std::cout << "Total reward: " << game->getTotalReward() << "\n";
        std::cout << "************************\n";
        itog += game->getTotalReward();
    }

    std::cout << itog;

    // It will be done automatically in destructor but after close You can init it again with different settings.
    game->close();
    delete game;

}
