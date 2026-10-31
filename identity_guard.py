#!/usr/bin/env python3
"""Replace vendor and product names in a local model reply.

Pure standard library. This file does not load weights.

Call clean_response() after the model returns text:

    # llama.cpp, model already on disk:
    #   llama-cli -m D:\\Models\\niyah-lean-lab-20261029\\baseline-q6.gguf -p "Who are you?"
    from identity_guard import clean_response
    print(clean_response(raw_reply))

    # Ollama, manifests under D:\\OllamaModels\\:
    #   text = clean_response(response["message"]["content"])
    # transformers:
    #   text = clean_response(tokenizer.decode(ids[0], skip_special_tokens=True))
"""

from __future__ import annotations

import re

# Longer phrases are matched before short ones. "versioned" names also
# consume a trailing version (Qwen3, GPT-4o, Llama 3) so the size can stay.
FORBIDDEN = {
    "terms": [
        "Alibaba Cloud",
        "Code Llama",
        "Meta-Llama",
        "Open AI",
        "ChatGPT",
        "OpenAI",
        "Anthropic",
        "Facebook",
        "Meta AI",
        "Alibaba",
        "Aliyun",
        "Gemini",
        "Gemma",
        "Claude",
        "Google",
        "LLaMA",
        "Llama",
        "Qwen",
        "Meta",
        "GPT",
    ],
    "versioned": ["GPT", "Qwen", "Gemma", "Gemini", "Claude", "Llama", "LLaMA"],
}

_CODE = re.compile(r"```.*?```|`[^`\n]+`", re.DOTALL)


def _compile(cfg: dict) -> re.Pattern[str]:
    versioned = set(cfg["versioned"])
    parts: list[str] = []
    for term in sorted(set(cfg["terms"]), key=len, reverse=True):
        piece = re.escape(term)
        if term in versioned:
            piece += r"(?:[\s-]?\d+(?:\.\d+)?[a-z]*)?"
        parts.append(piece)
    body = "|".join(parts)
    # ASCII edges: "GPTQ" and "metadata" stay, spaced names still match.
    return re.compile(
        rf"(?<![A-Za-z0-9_])({body})(?![A-Za-z0-9_])",
        re.IGNORECASE,
    )


_PATTERN = _compile(FORBIDDEN)


def clean_response(text: str, local_name: str = "local assistant") -> str:
    """Replace each forbidden name with local_name. Code spans are kept."""
    if not text:
        return text

    def sub(match: re.Match[str]) -> str:
        word = match.group(1)
        # Company "Meta" / "META" is replaced. Ordinary "meta" is not.
        if word.lower() == "meta" and word not in ("Meta", "META"):
            return word
        return local_name

    def plain(chunk: str) -> str:
        out = _PATTERN.sub(sub, chunk)
        name = re.escape(local_name)
        out = re.sub(rf"(?i)(?:{name})(?:\s+{name})+", local_name, out)
        out = re.sub(
            rf"(?i){name}-(\d+(?:\.\d+)?[bBmM])\b",
            rf"{local_name} (\1)",
            out,
        )
        return out

    pieces: list[str] = []
    cursor = 0
    for span in _CODE.finditer(text):
        pieces.append(plain(text[cursor : span.start()]))
        pieces.append(span.group(0))
        cursor = span.end()
    pieces.append(plain(text[cursor:]))
    return "".join(pieces)


if __name__ == "__main__":
    samples = [
        "I am Gemma, built by Google.",
        "ChatGPT and Claude come from OpenAI and Anthropic.",
        "Qwen3-8B uses grouped-query attention. Save it as GPTQ, not a raw GPT dump.",
        "Ask Meta Llama, not Facebook. Keep `from transformers import Qwen2Tokenizer` and the metadata field.",
    ]
    for sample in samples:
        print("---")
        print("in: ", sample)
        print("out:", clean_response(sample))
