#include "wamp/autotag.h"

#include "wamp/work_key.h"

#include <algorithm>
#include <cmath>

namespace wamp {
namespace {

// Peso de cada eixo. Título e artista carregam o grosso: são o que identifica a gravação.
// Álbum e duração desempatam entre edições — importante quando o mesmo título aparece em
// coletânea, ao vivo e estúdio.
constexpr double kWeightTitle = 0.45;
constexpr double kWeightArtist = 0.35;
constexpr double kWeightAlbum = 0.10;
constexpr double kWeightDuration = 0.10;

// Duração: casar dentro de 3 s é casar. A partir daí a nota cai até zero em 15 s — é a mesma
// tolerância que o `norm/v1` usa para dizer "mesma gravação".
constexpr std::int64_t kDurationToleranceMs = 3000;
constexpr std::int64_t kDurationDecayMs = 12000;

// Limiares da gravação silenciosa (docs/ROADMAP.md, F4).
constexpr double kAutoScore = 0.90;
constexpr double kAutoMargin = 0.15;
constexpr double kReviewScore = 0.55;

/// Compara um campo pelos olhos do `norm/v1`: "Feat." anexado, sufixo de remaster e caixa
/// deixam de contar. Igual vale 1; um contendo o outro vale 0,8 — título cortado no arquivo
/// ("Lonely As You (Live)") ainda casa com o do catálogo.
double fieldMatch(WorkField field, const std::string& query, const std::string& candidate) {
  const std::string a = normalizeWorkField(field, query);
  const std::string b = normalizeWorkField(field, candidate);
  if (a.empty() || b.empty()) return 0.0;
  if (a == b) return 1.0;
  const std::string& shorter = a.size() < b.size() ? a : b;
  const std::string& longer = a.size() < b.size() ? b : a;
  if (shorter.size() >= 4 && longer.find(shorter) != std::string::npos) return 0.8;
  return 0.0;
}

double durationMatch(std::int64_t queryMs, std::int64_t candidateMs) {
  if (queryMs <= 0 || candidateMs <= 0) return 0.0;
  const std::int64_t gap = std::llabs(queryMs - candidateMs);
  if (gap <= kDurationToleranceMs) return 1.0;
  const double decay = static_cast<double>(gap - kDurationToleranceMs) / kDurationDecayMs;
  return std::max(0.0, 1.0 - decay);
}

}  // namespace

double autotagScore(const TagQuery& query, const TagCandidate& candidate,
                    std::vector<std::string>* reasons) {
  struct Axis {
    std::string name;
    double weight;
    bool present;
    double match;
  };

  // O peso só conta para os eixos que a consulta tem: faixa sem álbum não pode ser punida por
  // um eixo que ela não oferece, senão "solta" nunca pontua.
  const Axis axes[] = {
      {"title", kWeightTitle, !normalizeWorkField(WorkField::Title, query.title).empty(),
       fieldMatch(WorkField::Title, query.title, candidate.title)},
      {"artist", kWeightArtist, !normalizeWorkField(WorkField::Artist, query.artist).empty(),
       fieldMatch(WorkField::Artist, query.artist, candidate.artist)},
      {"album", kWeightAlbum, !normalizeWorkField(WorkField::Album, query.album).empty(),
       fieldMatch(WorkField::Album, query.album, candidate.album)},
      {"duration", kWeightDuration, query.durationMs > 0,
       durationMatch(query.durationMs, candidate.durationMs)},
  };

  double total = 0.0;
  double available = 0.0;
  if (reasons != nullptr) reasons->clear();
  for (const Axis& axis : axes) {
    if (!axis.present) continue;
    available += axis.weight;
    if (axis.match >= 0.8) {
      total += axis.weight * axis.match;
      if (reasons != nullptr) reasons->push_back(axis.name);
    }
  }
  if (available <= 0.0) return 0.0;
  // Normaliza para 0..1 mesmo quando algum eixo da consulta ficou de fora.
  return std::clamp(total / available, 0.0, 1.0);
}

std::vector<ScoredCandidate> rankCandidates(const TagQuery& query,
                                            const std::vector<TagCandidate>& candidates,
                                            std::size_t limit) {
  std::vector<ScoredCandidate> scored;
  scored.reserve(candidates.size());
  for (const TagCandidate& candidate : candidates) {
    ScoredCandidate entry;
    entry.candidate = candidate;
    entry.score = autotagScore(query, candidate, &entry.reasons);
    scored.push_back(std::move(entry));
  }

  std::stable_sort(scored.begin(), scored.end(), [](const ScoredCandidate& a, const ScoredCandidate& b) {
    if (std::abs(a.score - b.score) > 1e-9) return a.score > b.score;
    // Empate: fica na frente quem tem mais motivos — mais casa, menos chute.
    return a.reasons.size() > b.reasons.size();
  });

  for (std::size_t i = 0; i < scored.size(); ++i) {
    const double next = i + 1 < scored.size() ? scored[i + 1].score : 0.0;
    scored[i].margin = scored[i].score - next;
  }
  if (scored.size() > limit) scored.resize(limit);
  return scored;
}

TagDecision classifyTagMatch(const std::vector<ScoredCandidate>& ranked) {
  if (ranked.empty()) return TagDecision::None;
  const ScoredCandidate& best = ranked.front();
  if (best.score >= kAutoScore && best.margin >= kAutoMargin) return TagDecision::AutoApply;
  if (best.score >= kReviewScore) return TagDecision::Review;
  return TagDecision::None;
}

TagFill planTagFill(const TagQuery& current, const TagCandidate& candidate) {
  TagFill fill;
  if (current.title.empty() && !candidate.title.empty()) fill.title = true;
  if (current.artist.empty() && !candidate.artist.empty()) fill.artist = true;
  if (current.album.empty()) {
    if (!candidate.album.empty()) fill.album = true;
    if (!candidate.albumArtist.empty()) fill.albumArtist = true;
  }
  if (current.trackNo == 0 && candidate.trackNo > 0) fill.trackNo = true;
  if (current.discNo == 0 && candidate.discNo > 0) fill.discNo = true;
  if (current.year == 0 && candidate.year > 0) fill.year = true;
  return fill;
}


TagFill consensusFill(const TagQuery& current, const std::vector<TagCandidate>& candidates,
                      double bestScore) {
  TagFill fill;
  if (candidates.empty() || bestScore < kConsensusScore) return fill;

  // Só os melhores, e só os que estão dentro da faixa de plausibilidade.
  std::vector<const TagCandidate*> plausible;
  for (const auto& candidate : candidates) {
    if (plausible.size() >= kConsensusDepth) break;
    plausible.push_back(&candidate);
  }

  auto allAgreeText = [&plausible](std::string TagCandidate::*field, bool fileEmpty) {
    if (!fileEmpty) return false;
    const std::string& first = plausible.front()->*field;
    if (first.empty()) return false;
    for (const auto* candidate : plausible) {
      if (normalizeWorkField(WorkField::Title, candidate->*field) !=
          normalizeWorkField(WorkField::Title, first)) {
        return false;
      }
    }
    return true;
  };
  auto allAgreeInt = [&plausible](int TagCandidate::*field, bool fileEmpty) {
    if (!fileEmpty) return false;
    const int first = plausible.front()->*field;
    if (first <= 0) return false;
    for (const auto* candidate : plausible) {
      if (candidate->*field != first) return false;
    }
    return true;
  };

  fill.title = allAgreeText(&TagCandidate::title, current.title.empty());
  fill.artist = allAgreeText(&TagCandidate::artist, current.artist.empty());
  fill.album = allAgreeText(&TagCandidate::album, current.album.empty());
  fill.albumArtist = allAgreeText(&TagCandidate::albumArtist, current.album.empty());
  fill.trackNo = allAgreeInt(&TagCandidate::trackNo, current.trackNo == 0);
  fill.discNo = allAgreeInt(&TagCandidate::discNo, current.discNo == 0);

  // O ano: o mais antigo entre os plausíveis que tenham o mesmo álbum do consenso, desde que a
  // defasagem entre o mais antigo e o mais novo seja de edição, não de outra obra.
  if (current.year == 0) {
    const std::string albumKey = plausible.front()->album.empty()
                                     ? std::string()
                                     : normalizeWorkField(WorkField::Album, plausible.front()->album);
    int oldest = 0;
    int newest = 0;
    for (const auto* candidate : plausible) {
      if (candidate->year <= 0) continue;
      if (!albumKey.empty() &&
          normalizeWorkField(WorkField::Album, candidate->album) != albumKey) {
        continue;
      }
      if (oldest == 0 || candidate->year < oldest) oldest = candidate->year;
      if (candidate->year > newest) newest = candidate->year;
    }
    if (oldest > 0 && newest - oldest <= kYearSpreadLimit) fill.year = true;
  }
  return fill;
}

bool tagWorthSearching(const TagQuery& query) { return !query.title.empty() || !query.artist.empty(); }

}  // namespace wamp
