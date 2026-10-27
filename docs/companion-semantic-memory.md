# Local semantic companion memory

Settings → Assistant → **Semantic memory** enables local hybrid retrieval. It is on
by default and requires **Conversation memory** and **Notes about you**. The model
starts lazily at the first chat; normal keyword memory remains usable during indexing
or if the model is unavailable. The setting can be disabled without deleting memories.

## Data preservation and lifecycle

- Existing `companion_memory.dat`, `companion_mind.dat`, `companion_cognition.dat`
  and archived conversation files remain the source of truth.
- Indexing is a read-only projection of notes, summaries, reflections, behavioral
  schemas, learned concepts, key relationship memories, shared phrases and named
  entities. Private off-screen episodes and hidden thoughts are not indexed.
- Only `companion_vectors.dat` is new: a SQLite database serialized and encrypted
  using Windows DPAPI. SQLite runs in memory; there is no plaintext database or WAL
  on disk. It contains content hashes, normalized vectors and the encoder identity,
  not raw conversation text.
- The first chat schedules existing memories on a background worker. Later changes
  reuse identical content vectors. Changed text gets a new content hash. Access
  metadata changes do not require re-embedding.
- Queries filter the current source snapshot by the companion's ownership/knowledge
  before ranking. Deleted or revoked entries are immediately ineligible; stale
  vectors are pruned by the worker. Hearsay and guesses keep their evidence labels.
- Clearing memory stops the index worker before removing its derived file. A stale
  worker cannot recreate the cleared index. Disabling the feature preserves it.
- A missing/corrupt/stale-model vector cache can be rebuilt without resetting the
  source archives. Portable companion backups carry source data and the setting;
  a restored copy builds its own index.

The compatibility limits retain up to 5,000 notes and 5,000 summaries (1,000 per
character), 1,000 chats per character, 1,200 reflections, 400 schemas, 4,000 concept
links, 256 taught concepts/key memories and 128 shared phrases/world entities per
character. Existing confirmation metadata is retained. These storage limits do
not expand the prompt budget.

## Retrieval

The encoder is `sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2`, with
Xenova's pinned quantized ONNX conversion. It supports Polish and other languages,
uses the original XLM-R SentencePiece tokenizer, masked mean pooling and L2
normalization. Inputs are bounded to 128 tokens; vectors have 384 float32 values.
CPU inference uses at most two intra-op threads and one inter-op thread. No GPU,
Python, Ollama, embedding API key or runtime network request is required.

SQLite stores vectors; exact cosine scoring in C++ is sufficient at this scale.
Keyword and vector ranks are combined with reciprocal-rank fusion, with small
importance/confirmation/freshness bonuses. Cosine similarity is not a probability.
Semantic-only candidates below 0.30 are rejected rather than forced into a reply.
Retrieval remains fallible, especially with slang, negation and ambiguous text.

Automatic recall contributes at most two candidates to the existing 5–7-item,
roughly 1,700-character workspace. The `przypomnij` tool uses hybrid results too.
Source facts are not rewritten from search results, and retrieval does not train
the language model.

## Build and distribution

`cmake/CompanionEmbeddings.cmake` pins all dependencies:

- ONNX Runtime CPU 1.23.2 (MIT).
- SentencePiece 0.2.1, commit `31646a467d2051eb904e0b45de3a73e91fe1c1e3` (Apache-2.0).
- SQLite 3.50.4 (public domain).
- Quantized model revision `2c4055b12046f11709e9df2c122e59ffbdc2f900` (Apache-2.0).

The model is approximately 118 MB, and the tokenizer approximately 5 MB; ONNX
Runtime adds its DLL. These are downloaded and digest-verified during configuration,
copied beside the executable, and included in the Runtime install component with
license notices. The original model/tokenizer digests are also checked before loading.
10,000 cached vectors require about 15.4 MB before SQLite/encryption overhead.

Build normally with CMake/Visual Studio. Tests named `companion_semantic_*` cover
cache migration/reuse, access and hearsay, edits/deletion, in-flight invalidation,
missing/corrupt model-cache fallback, original-data preservation, real Polish and
cross-language retrieval, the actual prompt/tool path, settings, and a fresh Runtime
installation. The real-model test uses synthetic examples and never reads a real profile.

For an isolated branch preview, set `LAUNCHME_CONFIG_DIR` to a fresh absolute test
directory before launching. To use existing memories later, run normally after
closing the other launcher; the new index is added alongside the existing files.
