#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <aws/core/Aws.h>
#include <aws/core/utils/json/JsonSerializer.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/core/utils/logging/ConsoleLogSystem.h>
#include <aws/core/utils/logging/LogMacros.h>
#include <aws/core/platform/Environment.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/lambda-runtime/runtime.h>

using namespace aws::lambda_runtime;

char const TAG[] = "LAMBDA_ALLOC";

class MonteCarloSimThread {
    private:
       int _num_sims;    	//no of simulated asset paths
       double _S;        	//option price
       double _K;      		//strike price
       double _r;	        //risk-free rate
       double _v;         	//volatility of the underlying (20%)
       double _T;         	//one year until expiry


        std::random_device rd;
        std::mt19937 gen{rd()};
        std::normal_distribution<double> distribution{0.0, 1.0};

        double gaussian_rnd() {
          return distribution(gen);
        }

        double monte_carlo_call_price(const int& num_sims, const double& S, const double& K, const double& r, const double& v, const double& T) {
                double S_adjust = S * exp(T*(r-0.5*v*v));
                double S_cur = 0.0;
                double payoff_sum = 0.0;

                for(int i=0; i < num_sims; i++) {
                        double gauss_bm = gaussian_rnd();
                        S_cur = S_adjust * exp(sqrt(v*v*T)*gauss_bm);
                        payoff_sum += std::max(S_cur - K, 0.0);
                        printThreadAllocation(i);
                }

                return (payoff_sum / static_cast<double>(num_sims)) * exp(-r*T);
        }


        double monte_carlo_put_price(const int& num_sims, const double& S, const double& K, const double& r, const double& v, const double& T) {
                double S_adjust = S * exp(T*(r-0.5*v*v));
                double S_cur = 0.0;
                double payoff_sum = 0.0;

                for(int i=0; i < num_sims; i++) {
                        double gauss_bm = gaussian_rnd();
                        S_cur = S_adjust * exp(sqrt(v*v*T)*gauss_bm);
                        payoff_sum += std::max(K - S_cur, 0.0);
                        printThreadAllocation(i);
                }

                return (payoff_sum / static_cast<double>(num_sims)) * exp(-r*T);
        }

        void printThreadAllocation(int i) {
                if (i % 1000000 == 0) {
			std::cerr << "Processed #" << i  << " paths\n";
                }
        }

	public:


        MonteCarloSimThread(const int& _num_sims, const double& _S, const double& _K, const double& _r, const double& _v, const double& _T) 
	{
		this -> _num_sims = _num_sims;
                this -> _S = _S;
                this -> _K = _K;
                this -> _r = _r;
                this -> _v = _v;
                this -> _T = _T;
	}

	void run(Aws::S3::S3Client const& s3client, std::string const& reqId) {
                double call = monte_carlo_call_price(_num_sims, _S, _K, _r, _v, _T);
                double put = monte_carlo_put_price(_num_sims, _S, _K, _r, _v, _T);

		Aws::StringStream ss;

		ss << "No of paths, Underlying, Strike, RiskFree Rate, Volatility, Maturity, Call Price, Put Price\n";
		ss << _num_sims << "," << _S << "," << _K << "," << _r << "," << _v << "," << _T << "," << call << "," << put << "\n";

		std::cerr << "No of paths           " << _num_sims << "\n";
		std::cerr << "Underlying:           " << _S << "\n";
		std::cerr << "Strike:               " << _K << "\n";
		std::cerr << "Risk-Free rate:       " << _r << "\n";
		std::cerr << "Volatility:           " << _v << "\n";
		std::cerr << "Maturity:             " << _T << "\n";
		std::cerr << "CALL Price:           " << call << "\n";
		std::cerr << "PUT  Price:           " << put << "\n";

		write_result_to_s3(ss.str().c_str(), s3client, reqId); 
        }

	bool write_result_to_s3(std::string const& message, Aws::S3::S3Client const s3client, std::string const& reqId) {
		Aws::S3::Model::PutObjectRequest request;                               
		auto bucketName = Aws::Environment::GetEnv("RESULT_BUCKET");
		auto objectPrefix = Aws::Environment::GetEnv("RESULT_PREFIX");
		request.SetBucket(bucketName);
		request.SetKey(objectPrefix + reqId + ".csv");
	
		std::shared_ptr<Aws::IOStream> data = Aws::MakeShared<Aws::StringStream>("");
		*data << message.c_str();
		data->flush();

		request.SetBody(data);
		request.SetContentLength(static_cast<long>(request.GetBody()->tellp()));
		request.SetContentType("text/plain");

		Aws::S3::Model::PutObjectOutcome outcome = s3client.PutObject(request);
	
		if (!outcome.IsSuccess()) {                                     
			std::cerr << "Error: PutObjectBuffer: " << outcome.GetError().GetMessage() << "\n";
			return false; 
		} else {
			std::cerr << "Success: Object '" << reqId << "' uploaded to bucket " << bucketName << "\n";
			return true;
		}
	}
};

static invocation_response my_handler(invocation_request const& req, Aws::S3::S3Client const s3client)
{
	using namespace Aws::Utils::Json;

//	JsonValue json(Aws::String(req.payload.c_str()));
	JsonValue json(req.payload);
	if (!json.WasParseSuccessful()) {
		return invocation_response::failure("Failed to parse input JSON", "InvalidJSON");
	}
	auto v = json.View();

	auto _num_sims = v.GetInteger("numberOfPaths");
        auto _S = v.GetDouble("underlyingPrice"); 
	auto _K = v.GetDouble("strikePrice");
	auto _v = v.GetDouble("volatility");
	constexpr double _r = 0.5;
	constexpr double _T = 1.0;

	MonteCarloSimThread(_num_sims, _S, _K, _r, _v, _T).run(s3client, req.request_id);
	return invocation_response::success("Simulation Finished!", "application/json");
}



std::function<std::shared_ptr<Aws::Utils::Logging::LogSystemInterface>()> GetConsoleLoggerFactory()
{
	return [] {
		return Aws::MakeShared<Aws::Utils::Logging::ConsoleLogSystem>(
			"console_logger", Aws::Utils::Logging::LogLevel::Trace);
	};
}

int main() 
{
	using namespace Aws;
	SDKOptions options;
	options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Trace;
	options.loggingOptions.logger_create_fn = GetConsoleLoggerFactory();
	InitAPI(options);
	{

		Client::ClientConfiguration config;
		config.region = Aws::Environment::GetEnv("AWS_REGION");
		config.caFile = "/etc/pki/tls/certs/ca-bundle.crt";

		auto credentialsProvider = Aws::MakeShared<Aws::Auth::EnvironmentAWSCredentialsProvider>(TAG);
		S3::S3Client s3client(credentialsProvider, config);
		auto handler_fn = [&s3client](aws::lambda_runtime::invocation_request const& req) {
			return my_handler(req, s3client);
		};	

		aws::lambda_runtime::run_handler(handler_fn);
	}
	ShutdownAPI(options);
	return 0;
}



