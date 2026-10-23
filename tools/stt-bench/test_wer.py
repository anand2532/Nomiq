import unittest

from wer import cer, corpus_wer, normalise, wer, wer_counts


class WerTest(unittest.TestCase):
    def test_identical(self):
        self.assertEqual(wer("Hello, World!", "hello world"), 0.0)

    def test_sub_del_ins(self):
        self.assertEqual(wer_counts("a b c d", "a x c d"), (1, 0, 0, 4))
        self.assertEqual(wer_counts("a b c d", "a c d"), (0, 1, 0, 4))
        self.assertEqual(wer_counts("a b c d", "a b c d e"), (0, 0, 1, 4))
        self.assertAlmostEqual(wer("the cat sat on the mat", "the cat sit on mat"), 2 / 6)

    def test_empty(self):
        self.assertEqual(wer("", ""), 0.0)
        self.assertEqual(wer("", "noise"), 1.0)
        self.assertEqual(wer("one two", ""), 1.0)

    def test_normalise(self):
        self.assertEqual(normalise("Don't  STOP—now."), ["dont", "stop", "now"])

    def test_corpus_is_weighted(self):
        pairs = [("a", "b"), ("a b c d e f g h i j", "a b c d e f g h i j")]
        self.assertAlmostEqual(corpus_wer(pairs), 1 / 11)

    def test_cer(self):
        self.assertAlmostEqual(cer("abcd", "abed"), 0.25)


if __name__ == "__main__":
    unittest.main()
