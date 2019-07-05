#include <random>
#include <iostream>
#include <fstream>

using namespace std;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: possion_gen <rate> <length>" << std::endl;
    return -1;
  }
  std::random_device rd;
  std::mt19937 gen(rd());
  int rate = atoi(argv[1]);
  int round = atoi(argv[2]);

  double lambda = 1 / static_cast<double>(rate);

  std::exponential_distribution<> dist(lambda);
  std::vector<int> ipts(round, 0);

  for (size_t i = 0; i < round; i++) {
    ipts[i] = dist(gen);
  }

  std::ofstream output("intervals.txt", ios::out);
  if (output.is_open()) {
    for (size_t i = 0; i < ipts.size(); i++) {
      output << ipts[i] << " "; 
    }

    output.close();
  }


}
