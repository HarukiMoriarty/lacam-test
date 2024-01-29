#include <argparse/argparse.hpp>
#include <chrono> 
#include <lacam.hpp>

int main(int argc, char* argv[])
{
  auto console = spdlog::stderr_color_mt("console");
  console->set_level(spdlog::level::info);

  // arguments parser
  argparse::ArgumentParser program("lacam", "0.1.0");
  program.add_argument("-m", "--map").help("map file").required();                                              // map file
  program.add_argument("-ng", "--ngoals").help("number of goals").required();                                   // number of goals: agent first go to get goal, and then return to unloading port
  program.add_argument("-na", "--nagents").help("number of agents").required();                                 // number of agents
  program.add_argument("-s", "--seed").help("seed").default_value(std::string("0"));                            // random seed
  program.add_argument("-v", "--verbose").help("verbose").default_value(std::string("0"));                      // verbose
  program.add_argument("-t", "--time_limit_sec").help("time limit sec").default_value(std::string("10"));       // time limit (second)
  program.add_argument("-o", "--output").help("output file").default_value(std::string("./result/result.txt")); // output file
  program.add_argument("-l", "--log_short").default_value(false).implicit_value(true);
  program.add_argument("-d", "--debug").help("enable debug logging").default_value(false).implicit_value(true); // debug mode

  try {
    program.parse_known_args(argc, argv);
  }
  catch (const std::runtime_error& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    std::exit(1);
  }

  // setup instance
  const auto verbose = std::stoi(program.get<std::string>("verbose"));
  const auto time_limit_sec = std::stoi(program.get<std::string>("time_limit_sec"));
  auto deadline = Deadline(time_limit_sec * 1000);
  const auto seed = std::stoi(program.get<std::string>("seed"));
  auto MT = std::mt19937(seed);
  const auto map_name = program.get<std::string>("map");
  const auto output_name = program.get<std::string>("output");
  const auto log_short = program.get<bool>("log_short");
  const auto ngoals = std::stoi(program.get<std::string>("ngoals"));
  const auto nagents = std::stoi(program.get<std::string>("nagents"));
  const auto debug = program.get<bool>("debug");
  if (debug)  console->set_level(spdlog::level::debug);
  auto ins = Instance(map_name, &MT, console, nagents, ngoals);

  // check paras
  if (!ins.is_valid(1)) {
    console->error("instance is invalid!");
    return 1;
  }
  if (nagents > ngoals) {
    console->error("number of goals must larger or equal to number of agents");
    return 1;
  }

  // output arguments info
  console->info("Map file:         {}", map_name);
  console->info("Number of goals:  {}", ngoals);
  console->info("Number of agents: {}", nagents);
  console->info("Seed:             {}", seed);
  console->info("Verbose:          {}", verbose);
  console->info("Time limit (sec): {}", time_limit_sec);
  console->info("Output file:      {}", output_name);
  console->info("Log short:        {}", log_short);
  console->info("Debug:            {}", debug);

  // initliaze log system
  Log log(console);
  // initliaze info timer
  auto timer = std::chrono::steady_clock::now();

  // solving
  uint nagents_with_new_goals = 0;
  uint step = 1;
  uint cache_hit = 0;
  uint cache_access = 0;
  for (int i = 0; i < ngoals; i += nagents_with_new_goals) {
    // info output
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(current_time - timer).count();

    if (!debug && elapsed_time >= 1 && cache_access > 0) {
      double cacheRate = static_cast<double>(cache_hit) / cache_access * 100.0;
      console->info("Elapsed Time: 1 second   |   Goals Reached: {:5}   |   Cache Rate: {:.2f}%    |   Steps Used: {:5}", i, cacheRate, step);
      // Reset the timer
      timer = std::chrono::steady_clock::now();
    }

    // ternimal log
    console->debug("--------------------------------------------------------------------------------------------------------------");
    console->debug("STEP:   {}", step);
    console->debug("STARTS: {}", ins.starts);
    console->debug("GOALS:  {}", ins.goals);

    // reset time clock
    assert(deadline.reset());

    auto solution = solve(ins, verbose - 1, &deadline, &MT);
    const auto comp_time_ms = deadline.elapsed_ms();

    // failure
    if (solution.empty()) {
      console->error("failed to solve");
      return 1;
    }

    // update step solution
    if (!log.update_solution(solution)) {
      console->error("Update step solution fails!");
      return 1;
    }

    // check feasibility
    if (!log.is_feasible_solution(ins, verbose)) {
      console->error("invalid solution");
      return 1;
    }

    // statistics
    step += solution.size();

    // post processing
    log.print_stats(verbose, ins, comp_time_ms);
    log.make_step_log(ins, output_name, comp_time_ms, map_name, seed, log_short);

    // assign new goals
    nagents_with_new_goals = ins.update_on_reaching_goals(solution, ngoals - i, cache_access, cache_hit);
    console->debug("Reached Goals: {}", nagents_with_new_goals);
  }

  log.make_life_long_log(ins, seed);

  return 0;
}
