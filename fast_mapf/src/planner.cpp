#include "../include/planner.hpp"

#include <algorithm>
#include <random>

float get_cost(Config& C, const DistTable& dist_table)
{
  float cost = 0;
  const auto N = C.size();
  for (auto i = 0; i < N; ++i) cost += dist_table.get(i, C[i]);
  return cost;
}

std::string get_id(Config& C)
{
  std::string id = "";
  for (auto v : C) id += std::to_string(v->id) + "-";
  return id;
}

std::vector<float> get_priorities(Config& C, const DistTable& dist_table,
                                  Node* parent)
{
  const auto N = C.size();
  auto P = std::vector<float>(C.size(), 0);
  if (parent == nullptr) {
    for (auto i = 0; i < N; ++i) P[i] = dist_table.get(i, C[i]) / N;
  } else {
    for (auto i = 0; i < N; ++i) {
      if (dist_table.get(i, C[i]) != 0) {
        P[i] = parent->priorities[i] + 1;
      } else {
        P[i] = parent->priorities[i] - (int)parent->priorities[i];
      }
    }
  }
  return P;
}

std::vector<int> get_order(Config& C, const std::vector<float>& priorities)
{
  std::vector<int> A(C.size());
  std::iota(A.begin(), A.end(), 0);
  std::sort(A.begin(), A.end(),
            [&](int i, int j) { return priorities[i] > priorities[j]; });
  return A;
}

Node::Node(Config _C, const DistTable& dist_table, std::string _id = "",
           Node* _parent = nullptr)
    : C(_C),
      cost(get_cost(_C, dist_table)),
      id(_id == "" ? get_id(_C) : _id),
      parent(_parent),
      depth(_parent == nullptr ? 0 : _parent->depth + 1),
      priorities(get_priorities(_C, dist_table, _parent)),
      order(get_order(_C, priorities)),
      search_tree(std::queue<Constraint*>())
{
  search_tree.push(new Constraint());
}

Node::~Node()
{
  while (!search_tree.empty()) {
    delete search_tree.front();
    search_tree.pop();
  }
}

Solution solve(const Instance& ins, const int verbose, const Deadline* deadline,
               std::mt19937* MT)
{
  const auto N = ins.N;
  const auto K = size(ins.G.V);

  // create distance table
  const auto dist_table = DistTable(ins);

  // setup PIBT
  auto occupied_now = Agents(K, nullptr);
  auto occupied_next = Agents(K, nullptr);
  Agents A(N, nullptr);
  for (auto i = 0; i < N; ++i) A[i] = new Agent(i);

  // setup search lists
  auto cmp_node = [](Node* a, Node* b) {
    if (a->depth != b->depth) return a->depth < b->depth;
    return a->cost > b->cost;
  };
  std::priority_queue<Node*, Nodes, decltype(cmp_node)> OPEN(cmp_node);
  std::unordered_map<std::string, Node*> EXPLORED;
  std::vector<Constraint*> GC;  // garbage collection for constraint

  // insert initial node
  auto S = new Node(ins.starts, dist_table);
  OPEN.push(S);
  EXPLORED[S->id] = S;

  // best first search
  int loop_cnt = 0;
  std::vector<Config> solution;

  while (!OPEN.empty() && !is_expired(deadline)) {
    loop_cnt += 1;

    // do not pop here!
    S = OPEN.top();

    // check goal condition
    if (is_same_config(S->C, ins.goals)) {
      // backtrack
      while (S != nullptr) {
        solution.push_back(S->C);
        S = S->parent;
      }
      std::reverse(solution.begin(), solution.end());
      break;
    }

    // search end
    if (S->search_tree.empty()) {
      OPEN.pop();
      continue;
    }

    // create successor for low-level search
    auto M = S->search_tree.front();
    GC.push_back(M);
    S->search_tree.pop();
    if (M->depth < N) {
      auto i = S->order[M->depth];
      auto C = S->C[i]->neighbor;
      std::sort(C.begin(), C.end(), [&](Vertex* a, Vertex* b) {
        return dist_table.get(i, a) < dist_table.get(i, b);
      });
      C.push_back(S->C[i]);
      for (auto u : C) S->search_tree.push(new Constraint(M, i, u));
    }

    // create successor for high-level search by PIBT
    {
      // setup occupied_now
      for (auto a : A) {
        // clear previous cache
        if (a->v_now != nullptr && occupied_now[a->v_now->id] == a) {
          occupied_now[a->v_now->id] = nullptr;
        }
        if (a->v_next != nullptr) {
          occupied_next[a->v_next->id] = nullptr;
          a->v_next = nullptr;
        }

        // set occupied now
        a->v_now = S->C[a->id];
        occupied_now[a->v_now->id] = a;
      }

      // setup constraint
      bool invalid = false;
      for (auto k = 0; k < M->depth; ++k) {
        const auto i = M->who[k];        // agent
        const auto l = M->where[k]->id;  // loc

        // check vertex collision
        if (occupied_next[l] != nullptr) {
          invalid = true;
          break;
        }
        // check swap collision
        auto l_pre = S->C[i]->id;
        if (occupied_next[l_pre] != nullptr && occupied_now[l] != nullptr &&
            occupied_next[l_pre]->id == occupied_now[l]->id) {
          invalid = true;
          break;
        }

        // set occupied_next
        A[i]->v_next = M->where[k];
        occupied_next[l] = A[i];
      }
      if (invalid) continue;

      if (M->depth == 1) {
        int c = 0;
        int k = -1;
        int l = 0;
        for (auto i = 0; i < N; ++i) {
          auto d = dist_table.get(i, S->C[i]);
          if (d > 0) l += 1;
          if (d > c) {
            k = i;
            c = d;
          }
        }
      }

      // run PIBT
      for (auto k : S->order) {
        auto a = A[k];
        if (a->v_next == nullptr) {
          if (!funcPIBT(a, nullptr, occupied_now, occupied_next, dist_table,
                        MT)) {
            invalid = true;
            break;
          }
        }
      }
      if (invalid) continue;

      // create new configuration
      auto C = Config(N, nullptr);
      for (auto a : A) C[a->id] = a->v_next;

      // check explored list
      auto S_new_id = get_id(C);
      if (EXPLORED.find(S_new_id) != EXPLORED.end()) {
        continue;
      }

      // insert new search node
      auto S_new = new Node(C, dist_table, S_new_id, S);
      OPEN.push(S_new);
      EXPLORED[S_new->id] = S_new;
    }
  }

  // memory management
  for (auto a : A) delete a;
  for (auto M : GC) delete M;
  for (auto p : EXPLORED) delete p.second;

  return solution;
}

bool funcPIBT(Agent* ai, Agent* aj, Agents& occupied_now, Agents& occupied_next,
              const DistTable& dist_table, std::mt19937* MT)
{
  // compare two nodes
  auto cmp = [&](Vertex* const v, Vertex* const u) {
    return dist_table.get(ai->id, v) < dist_table.get(ai->id, u);
    // tie break
    if (occupied_now[v->id] != nullptr && occupied_now[u->id] == nullptr)
      return false;
    if (occupied_now[v->id] == nullptr && occupied_now[u->id] != nullptr)
      return true;
    return false;
  };

  // get candidates
  auto C = ai->v_now->neighbor;
  C.push_back(ai->v_now);
  // randomize  <- important!
  if (MT != nullptr) std::shuffle(C.begin(), C.end(), *MT);
  // sort
  std::sort(C.begin(), C.end(), cmp);

  for (auto u : C) {
    // avoid vertex conflicts
    if (occupied_next[u->id] != nullptr) continue;
    // avoid swap conflicts
    if (aj != nullptr && u == aj->v_now) continue;

    auto ak = occupied_now[u->id];

    // avoid swap confilicts with constraints
    if (ak != nullptr && ak->v_next == ai->v_now) continue;

    // reserve
    occupied_next[u->id] = ai;
    ai->v_next = u;

    // empty or stay
    if (ak == nullptr || u == ai->v_now) return true;

    // priority inheritance
    if (ak->v_next == nullptr) {
      if (!funcPIBT(ak, ai, occupied_now, occupied_next, dist_table, MT))
        continue;  // replanning
    }
    // success to plan next one step
    return true;
  }

  // failed to secure node
  occupied_next[ai->v_now->id] = ai;
  ai->v_next = ai->v_now;
  return false;
}
