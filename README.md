
## HW3: Expr to Brainfuck 編譯器 - 112062102
---

Advanced C++: Large-Scale Projects and Object-Oriented Design Homework 3: Expression to Brainfuck Compiler

---



## 發想


看到infix就想建個expression tree然後照翻(直接讓brainfuck執行這個expression tree)，想說這有什麼加速空間呢，問了Gemini notebook原來是不能照翻，某些操作很貴
1. 比起乘法，寧可直接用迴圈相加
2. <>相當耗時，建議先算地址不要一格格退；用不到的就清掉減少總使用格數
3. 保存已經用不到的格子


測試原始指令速度
---
Claude test in python

Everything expensive is a **loop over a runtime value** (about 128 iterations on average).
- Move or copy a value: `[->+<]` is `5v+1`. Copy to two cells is `8v+1`.
- Constant add: `min(k, 256-k)` steps. 
- Multiply by a constant k: `(k'+4)*v`, with `k' = min(k, 256-k)`.
- Multiply two runtime values (naive nested loop): about **250k steps on average**, up to about 1M.
  I measured this with the real runner.
- The random benchmark cases contain many `var*var`, `c*c` and 3-factor products.

## 縮時

1. Constant folding and value numbering (CSE) over canonical polynomials mod 256 | Big, cheap. Use the polynomial form only as an equality key, not as the emission plan. Expanding `(b+c)*(a+d)` costs 4 products instead of 1. Cap expansion size. |

2. Track each cell as `atom + known offset`. `a=a+1` costs 1 step, and `print(a+k)` costs `+k . -k` | Big, cheap |
3. Track known cell contents (e.g. 0 after `[-]`), so no redundant clearing. `,` overwrites, so no clearing before input | Cheap |
4. Consume (move) a value at its last use. Copy only when it is still live. A copy loop can also create the variable's new home, so there is no restore pass | Medium |
5. Locality: put temps and destinations next to the source (pointer moves cost 1 each) | Medium |
6. Sign trick, `min(v, 256-v)` | **Rejected.** Reading a raw cell costs O(v) before any adaptivity. It only helps near 255 and gives about 33k per product on average. |

7. **Nibble split (radix 16) multiplication** | **Best idea found.** 


---
Nibble split 是什麼啊？？？查了網路說是將大數分段，可是最大也就256，於是詢問claude...因為有許多乘法算式，容易出現大於256的數，分段取16的mod可以在執行之前就直接免除無謂的循環。
> Worked example: 173 × 197 mod 256
> 
> 173 = 16·10 + 13, so uh=10 and ul=13.
> 197 = 16·12 + 5, so vh=12 and vl=5.
> ul·vl = 13·5 = 65.
> uh·vl = 10·5 = 50, and ul·vh = 13·12 = 156.
> Result: 65 + 16·(50 + 156) = 65 + 3296 = 3361. 3361 mod 256 = 33.
> Check: 173·197 = 34081, and 34081 mod 256 = 33. ✓ 

我覺得他說的是非常好啊。其他的點子大概跟一開始在概念階段就被提出的想法差不多，還有一些針對性很高的優化......本來是不想套用的但她還是自己加上去了。

## 實作
Gemini 3.8 Flash
* Constant Folding: Precomputes constants at compile time mod 256.
> 常數先計算可以吧。
* Canonical Affine Forms: Automatically simplifies linear combinations (e.g. $a - a = 0$, $a \cdot 16 \cdot 16 = 0$, $(a + 200) + 56 = a$).
>針對性很高的那一種，實在不知道這有什麼用。
* Common Subexpression Elimination (CSE): Canonicalizes products $u \cdot v = v \cdot u$ so repeated products/expressions reuse the same computed cells.
>存在比較常用的那一格
* Dead Code Elimination (DCE): Prunes all dead computations while preserving runtime input() calls.
>釋放資源
* Copy Propagation: Variable assignments like $b = a$ act as zero-cost aliases.
* Nibble Multiplication (Radix-16): Splits $u$ and $v$ into 4-bit nibbles, dropping the high-order $256 u_{hi} v_{hi}$ term and computing products via 3 small sub-products with loops $\le 15$ iterations instead of $\le 255$.
* Dedicated Squaring: Computes $u^2 \equiv u_{lo}^2 + 32(u_{lo} u_{hi}) \pmod{256}$ with only 2 sub-products.
>剛剛沒看過的新東西，一樣是乘法公式，是在nibble前提下對平方的優化
* In-place Offset Printing: Prints $V + k$ directly using + . - on existing cells without allocating or copying cells.
