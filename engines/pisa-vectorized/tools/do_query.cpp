#include <iostream>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <codec/block_codec_registry.hpp>
#include <cursor/block_max_scored_cursor.hpp>
#include <cursor/cursor.hpp>
#include <cursor/scored_cursor.hpp>
#include <cursor/vectorized_block_max_scored_cursor.hpp>
#include <index_types.hpp>
#include <memory_source.hpp>
#include <query.hpp>
#include <query/algorithm/and_query.hpp>
#include <query/algorithm/or_query.hpp>
#include <query/algorithm/ranked_and_query.hpp>
#include <query/algorithm/vectorized_bmm_query.hpp>
#include <query/query_parser.hpp>
#include <scorer/scorer.hpp>
#include <term_map.hpp>
#include <text_analyzer.hpp>
#include <tokenizer.hpp>
#include <topk_queue.hpp>
#include <wand_data.hpp>
#include <wand_data_raw.hpp>

static std::string const TERMLEX = "idx/fwd.termlex";  // term lexicon (payload vector)
static std::string const WAND = "idx/inv.bp.wand";     // block-max WAND data
static std::string const INDEX = "idx/inv.bp.simdbp";  // compressed inverted index

int main()
{
    using namespace pisa;

    spdlog::drop("");
    spdlog::set_default_logger(spdlog::stderr_color_mt(""));
    // Suppress per-query "term not found" warnings emitted by the query parser.
    spdlog::set_level(spdlog::level::err);

    auto index = BlockInvertedIndex(MemorySource::mapped_file(INDEX), get_block_codec("block_simdbp"));

    using WandType = wand_data<wand_data_raw>;
    WandType wdata(MemorySource::mapped_file(WAND));

    ScorerParams scorer_params("bm25");
    auto scorer = scorer::from_params(scorer_params, wdata);

    QueryParser parser(
        TextAnalyzer(std::make_unique<WhitespaceTokenizer>()),
        std::make_unique<LexiconMap>(TERMLEX));

    std::string line;
    while (std::getline(std::cin, line)) {
        bool intersection = false;
        size_t count = 0;
        std::vector<std::string> tokens;
        boost::split(tokens, line, boost::is_any_of("\t"));
        if (boost::contains(tokens[1], "\"")) {
            std::cout << "UNSUPPORTED\n";
            continue;
        }
        if (boost::starts_with(tokens[1], "+")) {
            intersection = true;
            boost::replace_all(tokens[1], "+", "");
        }

        Query query = parser.parse(tokens[1]);
        auto const& terms = query.terms();

        if (tokens[0] == "COUNT") {
            if (terms.size() == 1) {
                count = index[terms[0].id].size();
            } else if (intersection) {
                and_query and_q;
                count = and_q(make_cursors(index, query), index.num_docs()).size();
            } else {
                or_query<false> or_q;
                count = or_q(make_cursors(index, query), index.num_docs());
            }
        } else if (tokens[0] == "TOP_10" || tokens[0] == "TOP_100" || tokens[0] == "TOP_1000") {
            size_t k = 0;
            if (tokens[0] == "TOP_10") {
                k = 10;
            } else if (tokens[0] == "TOP_100") {
                k = 100;
            } else {
                k = 1000;
            }
            topk_queue topk(k);
            if (intersection || terms.size() == 1) {
                ranked_and_query ranked_and_q(topk);
                ranked_and_q(make_scored_cursors(index, *scorer, query), index.num_docs());
            } else {
                vectorized_bmm_query vec_bmm_q(topk);
                vec_bmm_q(
                    make_vectorized_block_max_scored_cursors(index, wdata, *scorer, query),
                    index.num_docs());
            }
            topk.finalize();
            count = 1;
        } else if (tokens[0] == "TOP_10_COUNT" || tokens[0] == "TOP_100_COUNT"
                   || tokens[0] == "TOP_1000_COUNT") {
            if (intersection || terms.size() == 1) {
                scored_and_query and_q;
                count = and_q(make_scored_cursors(index, *scorer, query), index.num_docs()).size();
            } else {
                or_query<true> or_q;
                count = or_q(make_cursors(index, query), index.num_docs());
            }
        } else {
            std::cout << "UNSUPPORTED\n";
            continue;
        }
        std::cout << count << "\n";
    }
}
