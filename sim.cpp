#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>
#include <random>

using namespace std;

class MonteCarloSimThread {
  private:
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::normal_distribution<double> distribution{0.0, 1.0};

    double gaussian_rnd() {
      return distribution(gen);
    }

    // Pricing a European vanilla call option with a Monte Carlo method
    double monte_carlo_call_price(const int& num_sims, const double& S, const double& K, const double& r, const double& v, const double& T) {
      double S_adjust = S * exp(T*(r-0.5*v*v));
      double S_cur = 0.0;
      double payoff_sum = 0.0;

      for (int i=0; i<num_sims; i++) {
        double gauss_bm = gaussian_rnd();
        S_cur = S_adjust * exp(sqrt(v*v*T)*gauss_bm);
        payoff_sum += std::max(S_cur - K, 0.0);
        //printThread(i);
      }

      return (payoff_sum / static_cast<double>(num_sims)) * exp(-r*T);
    }

    // Pricing a European vanilla put option with a Monte Carlo method
    double monte_carlo_put_price(const int& num_sims, const double& S, const double& K, const double& r, const double& v, const double& T) {
      double S_adjust = S * exp(T*(r-0.5*v*v));
      double S_cur = 0.0;
      double payoff_sum = 0.0;

      for (int i=0; i<num_sims; i++) {
        double gauss_bm = gaussian_rnd();
        S_cur = S_adjust * exp(sqrt(v*v*T)*gauss_bm);
        payoff_sum += std::max(K - S_cur, 0.0);
        //printThread(i);
      }

      return (payoff_sum / static_cast<double>(num_sims)) * exp(-r*T);
    }

    void printThread(int i) {
      if (i % 1000000 == 0) {
        cout << "Thread #" << std::this_thread::get_id() << ": on CPU " << sched_getcpu() << "\n";
      }
    }

  public:
    MonteCarloSimThread() {}

    void run(const int& num_sims, const double& S, const double& K, const double& r, const double& v, const double& T) {

      // calculate the call/put values via Monte Carlo
      double call = monte_carlo_call_price(num_sims, S, K, r, v, T);
      double put = monte_carlo_put_price(num_sims, S, K, r, v, T);

      cout << "THREAD:           " << this_thread::get_id() << endl;
      cout << " Number of Paths: " << num_sims << endl;
      cout << " Underlying:      " << S << endl;
      cout << " Strike:          " << K << endl;
      cout << " Risk-Free Rate:  " << r << endl;
      cout << " Volatility:      " << v << endl;
      cout << " Maturity:        " << T << endl;

      cout << " Call Price:      " << call << endl;
      cout << " Put Price:       " << put << endl << endl;
    }
};

int main(int argc, char **argv) {
  if (argc < 4) {
    std::cout << "Need 3 arguments: sim <num_of_montecarlo_paths_per_thread(int)> <num_threads(int)> <thread_affinity(0/1)>\n";
    return -1;
  }

  int num_sims = std::stoi(argv[1]);
  int num_threads = std::stoi(argv[2]);
  bool thread_affinity = std::stoi(argv[3]);

  int num_cpus = std::thread::hardware_concurrency();

  //parameter list for montecarlo option pricing
  constexpr double _S = 100.0;  // Option price
  constexpr double _K = 100.0;  // Strike price
  constexpr double _r = 0.05;   // Risk-free rate (5%)
  constexpr double _v = 0.2;    // Volatility of the underlying (20%)
  constexpr double _T = 1.0;    // One year until expiry

  std::vector < MonteCarloSimThread > vecOfObj(num_threads);
  std::vector < std::thread > vecOfThreads;

  cpu_set_t cpuset;

  cout << "Found " << num_cpus << " CPUs\n";

  //create threads and set affinity
  for (unsigned t=0; t < num_threads; t++) {
		auto &simThread = vecOfObj[t];

    vecOfThreads.push_back(std::thread(&MonteCarloSimThread::run, &simThread, num_sims, _S+t, _K, _r, _v, _T));
    cout << "Started thread " << t << endl;

    if (thread_affinity) {
      CPU_ZERO(&cpuset);
      CPU_SET(t%num_cpus, &cpuset);
      int rc = pthread_setaffinity_np(vecOfThreads[t].native_handle(), sizeof(cpu_set_t), &cpuset);
      if (rc != 0) {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
      }
    }
  }

  //tidy up all threads
  for (auto &thread : vecOfThreads) {
		thread.join();
  }

  return 0;
}
