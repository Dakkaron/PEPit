"""Client for the LM Studio decision endpoint (/v1/systemone).

Validated against cloudflare_clef-flash: accepts a text `state` block and/or
a list of base64 data-URL `images`, plus a map of typed questions
(choice / score / noul). Answers come back as probabilities — the raw
probabilities are the useful signal, not just the argmax.
"""

import json
import urllib.error
import urllib.request


class LLMClient:
    def __init__(self,
                 model: str = "cloudflare_clef-flash",
                 base_url: str = "http://192.168.0.68:1234",
                 timeout: float = 60.0):
        self.model = model
        self.endpoint = base_url.rstrip("/") + "/v1/systemone"
        self.timeout = timeout

    def decide(self, questions: dict,
               state_text: str | None = None,
               image: str | None = None) -> dict:
        """Ask typed questions; returns the raw answers dict.

        questions: {id: {"type": "choice"|"score"|"noul",
                         "instructions": str, "criteria": {...}}}
        state_text: deterministic extractor summary (text mode)
        image:      base64 data URL of the frame (vision mode; may combine
                    with state_text for the hybrid mode)
        """
        body = {"model": self.model, "questions": questions}
        if state_text is not None:
            body["state"] = state_text
        if image is not None:
            body["images"] = [image]

        req = urllib.request.Request(
            self.endpoint,
            data=json.dumps(body).encode(),
            headers={"Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            return {"error": f"HTTP {e.code}: {e.read().decode()[:300]}"}
        except Exception as e:  # timeout, connection refused, ...
            return {"error": f"{type(e).__name__}: {e}"}

    # ------------------------------------------------------------------
    @staticmethod
    def answer_choice(data: dict, qid: str):
        """(choice, confidence, probabilities) or (None, 0.0, {}) on error."""
        a = data.get("answers", {}).get(qid, {})
        probs = a.get("probabilities", {})
        return a.get("choice"), a.get("confidence", 0.0), probs

    @staticmethod
    def answer_noul(data: dict, qid: str) -> float:
        """Probability that the answer is Yes (1.0 = yes)."""
        return float(data.get("answers", {}).get(qid, {}).get("noul", 0.5))
