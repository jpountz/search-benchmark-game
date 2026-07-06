#include <filesystem>
#include <numeric>
#include <optional>
#include <string>

#include <fmt/format.h>
#include <mio/mmap.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <tbb/global_control.h>

#include <compress.hpp>
#include <document_record.hpp>
#include <forward_index_builder.hpp>
#include <invert.hpp>
#include <payload_vector.hpp>
#include <reorder_docids.hpp>
#include <scorer/scorer.hpp>
#include <text_analyzer.hpp>
#include <tokenizer.hpp>
#include <wand_data.hpp>
#include <wand_utils.hpp>

static std::size_t const THREADS = 2;
static std::size_t const BATCH_SIZE = 10'000;

// File layout under the index directory.
static std::string const IDX_DIR = "idx";
static std::string const FWD = "idx/fwd";              // forward index basename
static std::string const TERMLEX = "idx/fwd.termlex";  // term lexicon (payload vector)
static std::string const INV = "idx/inv";              // inverted index basename
static std::string const INV_BP = "idx/inv.bp";        // reordered inverted index basename
static std::string const WAND = "idx/inv.bp.wand";     // block-max WAND data
static std::string const INDEX = "idx/inv.bp.simdbp";  // compressed inverted index

using pisa::Document_Record;
using pisa::Forward_Index_Builder;
using pisa::TextAnalyzer;
using pisa::WhitespaceTokenizer;

// Parse the corpus (JSONL with "id" and "text" fields, as produced by
// corpus_transform.py) into a forward index. The corpus is already lowercased
// and stripped of non-alphabetic characters, so a whitespace tokenizer suffices
// and keeps indexing and querying perfectly consistent.
void parse()
{
    Forward_Index_Builder builder;
    auto analyzer = std::make_shared<TextAnalyzer>(std::make_unique<WhitespaceTokenizer>());
    builder.build(
        std::cin,
        FWD,
        [](std::istream& in) -> std::optional<Document_Record> {
            std::string line;
            if (std::getline(in, line) && not line.empty()) {
                auto record = nlohmann::json::parse(line);
                return std::make_optional<Document_Record>(
                    record["id"].get<std::string>(), record["text"].get<std::string>(), "");
            }
            return std::nullopt;
        },
        analyzer,
        BATCH_SIZE,
        THREADS);
}

void invert()
{
    mio::mmap_source mfile(TERMLEX.c_str());
    auto term_count = pisa::Payload_Vector<>::from(mfile).size();
    pisa::invert::InvertParams params;
    params.batch_size = BATCH_SIZE;
    params.num_threads = THREADS;
    params.term_count = term_count;
    pisa::invert::invert_forward_index(FWD, INV, params);
}

// Reorder document IDs with recursive graph bisection to improve compression and
// query performance. Term IDs are unchanged, so the term lexicon stays valid.
void reorder()
{
    pisa::recursive_graph_bisection(pisa::RecursiveGraphBisectionOptions{
        .input_basename = INV,
        .output_basename = INV_BP,
        .output_fwd = std::nullopt,
        .input_fwd = std::nullopt,
        .document_lexicon = std::nullopt,
        .reordered_document_lexicon = std::nullopt,
        .depth = std::nullopt,
        .node_config = std::nullopt,
        .min_length = 0,
        .compress_fwd = true,
        .print_args = false,
    });
}

void wand()
{
    pisa::create_wand_data(
        WAND,
        INV_BP,
        pisa::FixedBlock(128),
        ScorerParams("bm25"),
        false,          // range
        false,          // compress
        std::nullopt,   // quantization_bits
        {});            // dropped_term_ids
}

void compress()
{
    pisa::compress(
        INV_BP,
        std::nullopt,   // wand_data_filename (only needed when quantizing)
        "block_simdbp",
        INDEX,
        ScorerParams("bm25"),
        std::nullopt,   // quantization_bits
        false,          // check
        false);         // in_memory
}

int main()
{
    spdlog::drop("");
    spdlog::set_default_logger(spdlog::stderr_color_mt(""));

    tbb::global_control control(tbb::global_control::max_allowed_parallelism, THREADS + 1);

    std::filesystem::create_directories(IDX_DIR);

    parse();
    invert();
    reorder();
    wand();
    compress();
}
