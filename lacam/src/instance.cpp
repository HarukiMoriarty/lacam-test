#include "../include/instance.hpp"

Instance::Instance(const std::string& map_filename, std::mt19937* MT, std::shared_ptr<spdlog::logger> _logger, const int _nagents, const int _ngoals)
  : G(Graph(map_filename, MT)), starts(Config()), goals(Config()), nagents(_nagents), ngoals(_ngoals), logger(std::move(_logger))
{
  const auto K = G.size();

  // set agents random start potition
  auto s_indexes = std::vector<int>(K);
  std::iota(s_indexes.begin(), s_indexes.end(), 0);
  std::shuffle(s_indexes.begin(), s_indexes.end(), *MT);
  int i = 0;
  while (true) {
    if (i >= K) return;
    starts.push_back(G.V[s_indexes[i]]);
    if (starts.size() == nagents) break;
    ++i;
  }

  // set goals
  int j = 0;
  while (true) {
    if (j >= K) return;
    Vertex* goal = G.random_target_vertex();
    goals.push_back(goal);
    cargo_goals.push_back(goal);
    bit_status.push_back(0);              // at the begining, the cache is empty
    if (goals.size() == nagents) break;
    ++j;
  }
}

bool Instance::is_valid(const int verbose) const
{
  if (nagents != starts.size() || nagents != goals.size()) {
    info(1, verbose, "invalid N, check instance");
    return false;
  }
  return true;
}

int Instance::update_on_reaching_goals(std::vector<Config>& vertex_list, int remain_goals) {
  logger->debug("Remain goals: {}", remain_goals);
  int step = vertex_list.size() - 1;
  int reached_count = 0;

  // Check each vertex at this time step
  for (size_t j = 0; j < vertex_list[step].size(); ++j) {
    // TODO: assign goals to closed free agents
    if ((*vertex_list[step][j] == *goals[j]) && (remain_goals > 0)) {
      // Status 0 finished, agent has moved to warehouse cargo target
      if (bit_status[j] == 0) {
        Vertex* goal = G.cache.try_insert_cache(cargo_goals[j], G.unloading_ports[0]);
        // Cache full, directly get back to unloading port, -> Status 3
        if (goal == G.unloading_ports[0]) {
          logger->debug("Agent {} status 0 -> status 3, reach warehouse cargo {}, cache is full, go back to unloading port", j, *cargo_goals[j]);
          bit_status[j] = 3;
        }
        // Find empty cache block, go and insert cargo into cache, -> Status 2
        else {
          logger->debug("Agent {} status 0 -> status 2, reach warehouse cargo {}, find cache block to insert, go to cache block {}", j, *cargo_goals[j], *goal);
          bit_status[j] = 2;
        }

        // update goal
        goals[j] = goal;
      }
      // Status 1 finished, agent has moved to cache cargo target
      // update cache lock info, directly move back to unloading port, -> Status 3
      else if (bit_status[j] == 1) {
        logger->debug("Agent {} status 1 -> status 3, reach cached cargo {} at cache block {}, return to unloading port", j, *cargo_goals[j], *goals[j]);
        bit_status[j] = 3;
        G.cache.update_cargo_from_cache(cargo_goals[j], goals[j]);
        goals[j] = G.unloading_ports[0];
      }
      // Status 2 finished, agent has bring uncached cargo back to cache
      // update cache, move to unloading port, -> Status 3
      else if (bit_status[j] == 2) {
        logger->debug("Agent {} status 2 -> status 3, bring cargo {} to cache block {}, then return to unloading port", j, *cargo_goals[j], *goals[j]);
        bit_status[j] = 3;
        // update cache
        assert(G.cache.update_cargo_into_cache(cargo_goals[j], goals[j]));
        // update status and goal
        goals[j] = G.unloading_ports[0];
      }
      // Status 3 finished, agent has back to unloading port, assigned with new cargo target
      else if (bit_status[j] == 3) {
        // update statistics
        remain_goals--;
        reached_count++;

        logger->debug("Agent {} has bring cargo {} to unloading port", j, *cargo_goals[j]);
        // generate new cargo goal
        Vertex* cargo = G.random_target_vertex();
        cargo_goals[j] = cargo;
        Vertex* goal = G.cache.try_cache_cargo(cargo);

        // Cache hit, go to cache to get cached cargo, -> Status 1
        if (cargo != goal) {
          logger->debug("Agent {} assigned with new cargo {}, cache hit. Go to cache, status 3 -> status 1 {}", j, *cargo_goals[j], *goal);
          bit_status[j] = 1;
        }
        // Cache miss, go to warehouse to get cargo, -> Status 0
        else {
          logger->debug("Agent {} assigned with new cargo {}, cache miss. Go to warehouse, status 3 -> status 0", j, *cargo_goals[j]);
          bit_status[j] = 0;
        }
        // update goal
        goals[j] = goal;
      }
    }
  }

  starts = vertex_list[step];
  std::cerr << "Ends: " << starts << std::endl;
  return reached_count;
}
